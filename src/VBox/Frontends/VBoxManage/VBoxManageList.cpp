/* $Id: VBoxManageList.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - The 'list' command.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/VirtualBox.h>

#include <VBox/log.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/getopt.h>
#include <iprt/ctype.h>

#include <vector>
#include <algorithm>

#include "VBoxManage.h"
using namespace com;

DECLARE_TRANSLATION_CONTEXT(List);

#ifdef VBOX_WITH_HOSTNETIF_API
static const char *getHostIfMediumTypeText(HostNetworkInterfaceMediumType_T enmType)
{
    switch (enmType)
    {
        case HostNetworkInterfaceMediumType_Ethernet: return "Ethernet";
        case HostNetworkInterfaceMediumType_PPP: return "PPP";
        case HostNetworkInterfaceMediumType_SLIP: return "SLIP";
        case HostNetworkInterfaceMediumType_Unknown: return List::tr("Unknown");
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case HostNetworkInterfaceMediumType_32BitHack: break; /* Shut up compiler warnings. */
#endif
    }
    return List::tr("unknown");
}

static const char *getHostIfStatusText(HostNetworkInterfaceStatus_T enmStatus)
{
    switch (enmStatus)
    {
        case HostNetworkInterfaceStatus_Up: return List::tr("Up");
        case HostNetworkInterfaceStatus_Down: return List::tr("Down");
        case HostNetworkInterfaceStatus_Unknown: return List::tr("Unknown");
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case HostNetworkInterfaceStatus_32BitHack: break; /* Shut up compiler warnings. */
#endif
    }
    return List::tr("unknown");
}
#endif /* VBOX_WITH_HOSTNETIF_API */

static const char*getDeviceTypeText(DeviceType_T enmType)
{
    switch (enmType)
    {
        case DeviceType_HardDisk: return List::tr("HardDisk");
        case DeviceType_DVD: return "DVD";
        case DeviceType_Floppy: return List::tr("Floppy");
        /* Make MSC happy */
        case DeviceType_Null: return "Null";
        case DeviceType_Network:        return List::tr("Network");
        case DeviceType_USB:            return "USB";
        case DeviceType_SharedFolder:   return List::tr("SharedFolder");
        case DeviceType_Graphics3D:     return List::tr("Graphics3D");
        case DeviceType_End: break; /* Shut up compiler warnings. */
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case DeviceType_32BitHack: break; /* Shut up compiler warnings. */
#endif
    }
    return List::tr("Unknown");
}


/**
 * List internal networks.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listInternalNetworks(const ComPtr<IVirtualBox> pVirtualBox)
{
    HRESULT hrc;
    com::SafeArray<BSTR> internalNetworks;
    CHECK_ERROR(pVirtualBox, COMGETTER(InternalNetworks)(ComSafeArrayAsOutParam(internalNetworks)));
    for (size_t i = 0; i < internalNetworks.size(); ++i)
    {
        RTPrintf(List::tr("Name:        %ls\n"), internalNetworks[i]);
    }
    return hrc;
}


/**
 * List network interfaces information (bridged/host only).
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 * @param   fIsBridged          Selects between listing host interfaces (for
 *                              use with bridging) or host only interfaces.
 */
static HRESULT listNetworkInterfaces(const ComPtr<IVirtualBox> pVirtualBox,
                                     bool fIsBridged)
{
    HRESULT hrc;
    ComPtr<IHost> host;
    CHECK_ERROR(pVirtualBox, COMGETTER(Host)(host.asOutParam()));
    com::SafeIfaceArray<IHostNetworkInterface> hostNetworkInterfaces;
#if defined(VBOX_WITH_NETFLT)
    if (fIsBridged)
        CHECK_ERROR(host, FindHostNetworkInterfacesOfType(HostNetworkInterfaceType_Bridged,
                                                          ComSafeArrayAsOutParam(hostNetworkInterfaces)));
    else
        CHECK_ERROR(host, FindHostNetworkInterfacesOfType(HostNetworkInterfaceType_HostOnly,
                                                          ComSafeArrayAsOutParam(hostNetworkInterfaces)));
#else
    RT_NOREF(fIsBridged);
    CHECK_ERROR(host, COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(hostNetworkInterfaces)));
#endif
    for (size_t i = 0; i < hostNetworkInterfaces.size(); ++i)
    {
        ComPtr<IHostNetworkInterface> networkInterface = hostNetworkInterfaces[i];
#ifndef VBOX_WITH_HOSTNETIF_API
        Bstr interfaceName;
        networkInterface->COMGETTER(Name)(interfaceName.asOutParam());
        RTPrintf(List::tr("Name:        %ls\n"), interfaceName.raw());
        Guid interfaceGuid;
        networkInterface->COMGETTER(Id)(interfaceGuid.asOutParam());
        RTPrintf("GUID:        %ls\n\n", Bstr(interfaceGuid.toString()).raw());
#else /* VBOX_WITH_HOSTNETIF_API */
        Bstr interfaceName;
        networkInterface->COMGETTER(Name)(interfaceName.asOutParam());
        RTPrintf(List::tr("Name:            %ls\n"), interfaceName.raw());
        Bstr interfaceGuid;
        networkInterface->COMGETTER(Id)(interfaceGuid.asOutParam());
        RTPrintf("GUID:            %ls\n", interfaceGuid.raw());
        BOOL fDHCPEnabled = FALSE;
        networkInterface->COMGETTER(DHCPEnabled)(&fDHCPEnabled);
        RTPrintf("DHCP:            %s\n", fDHCPEnabled ? List::tr("Enabled") : List::tr("Disabled"));

        Bstr IPAddress;
        networkInterface->COMGETTER(IPAddress)(IPAddress.asOutParam());
        RTPrintf(List::tr("IPAddress:       %ls\n"), IPAddress.raw());
        Bstr NetworkMask;
        networkInterface->COMGETTER(NetworkMask)(NetworkMask.asOutParam());
        RTPrintf(List::tr("NetworkMask:     %ls\n"), NetworkMask.raw());
        Bstr IPV6Address;
        networkInterface->COMGETTER(IPV6Address)(IPV6Address.asOutParam());
        RTPrintf(List::tr("IPV6Address:     %ls\n"), IPV6Address.raw());
        ULONG IPV6NetworkMaskPrefixLength;
        networkInterface->COMGETTER(IPV6NetworkMaskPrefixLength)(&IPV6NetworkMaskPrefixLength);
        RTPrintf(List::tr("IPV6NetworkMaskPrefixLength: %d\n"), IPV6NetworkMaskPrefixLength);
        Bstr HardwareAddress;
        networkInterface->COMGETTER(HardwareAddress)(HardwareAddress.asOutParam());
        RTPrintf(List::tr("HardwareAddress: %ls\n"), HardwareAddress.raw());
        HostNetworkInterfaceMediumType_T Type;
        networkInterface->COMGETTER(MediumType)(&Type);
        RTPrintf(List::tr("MediumType:      %s\n"), getHostIfMediumTypeText(Type));
        BOOL fWireless = FALSE;
        networkInterface->COMGETTER(Wireless)(&fWireless);
        RTPrintf(List::tr("Wireless:        %s\n"), fWireless ? List::tr("Yes") : List::tr("No"));
        HostNetworkInterfaceStatus_T Status;
        networkInterface->COMGETTER(Status)(&Status);
        RTPrintf(List::tr("Status:          %s\n"), getHostIfStatusText(Status));
        Bstr netName;
        networkInterface->COMGETTER(NetworkName)(netName.asOutParam());
        RTPrintf(List::tr("VBoxNetworkName: %ls\n\n"), netName.raw());
#endif
    }
    return hrc;
}


#ifdef VBOX_WITH_VMNET
/**
 * List configured host-only networks.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 * @param   Reserved            Placeholder!
 */
static HRESULT listHostOnlyNetworks(const ComPtr<IVirtualBox> pVirtualBox)
{
    HRESULT hrc;
    com::SafeIfaceArray<IHostOnlyNetwork> hostOnlyNetworks;
    CHECK_ERROR(pVirtualBox, COMGETTER(HostOnlyNetworks)(ComSafeArrayAsOutParam(hostOnlyNetworks)));
    for (size_t i = 0; i < hostOnlyNetworks.size(); ++i)
    {
        ComPtr<IHostOnlyNetwork> hostOnlyNetwork = hostOnlyNetworks[i];
        Bstr bstrNetworkName;
        CHECK_ERROR2I(hostOnlyNetwork, COMGETTER(NetworkName)(bstrNetworkName.asOutParam()));
        RTPrintf(List::tr("Name:            %ls\n"), bstrNetworkName.raw());

        Bstr bstr;
        CHECK_ERROR(hostOnlyNetwork, COMGETTER(Id)(bstr.asOutParam()));
        RTPrintf("GUID:            %ls\n\n", bstr.raw());

        BOOL fEnabled = FALSE;
        CHECK_ERROR2I(hostOnlyNetwork, COMGETTER(Enabled)(&fEnabled));
        RTPrintf(List::tr("State:           %s\n"), fEnabled ? List::tr("Enabled") : List::tr("Disabled"));

        CHECK_ERROR2I(hostOnlyNetwork, COMGETTER(NetworkMask)(bstr.asOutParam()));
        RTPrintf(List::tr("NetworkMask:     %ls\n"), bstr.raw());

        CHECK_ERROR2I(hostOnlyNetwork, COMGETTER(LowerIP)(bstr.asOutParam()));
        RTPrintf(List::tr("LowerIP:         %ls\n"), bstr.raw());

        CHECK_ERROR2I(hostOnlyNetwork, COMGETTER(UpperIP)(bstr.asOutParam()));
        RTPrintf(List::tr("UpperIP:         %ls\n"), bstr.raw());

        // CHECK_ERROR2I(hostOnlyNetwork, COMGETTER(Id)(bstr.asOutParam());
        // RTPrintf("NetworkId:       %ls\n", bstr.raw());

        RTPrintf(List::tr("VBoxNetworkName: hostonly-%ls\n\n"), bstrNetworkName.raw());
    }
    return hrc;
}
#endif /* VBOX_WITH_VMNET */


#ifdef VBOX_WITH_CLOUD_NET
/**
 * List configured cloud network attachments.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 * @param   Reserved            Placeholder!
 */
static HRESULT listCloudNetworks(const ComPtr<IVirtualBox> pVirtualBox)
{
    com::SafeIfaceArray<ICloudNetwork> cloudNetworks;
    CHECK_ERROR2I_RET(pVirtualBox, COMGETTER(CloudNetworks)(ComSafeArrayAsOutParam(cloudNetworks)), hrcCheck);
    for (size_t i = 0; i < cloudNetworks.size(); ++i)
    {
        ComPtr<ICloudNetwork> cloudNetwork = cloudNetworks[i];
        Bstr networkName;
        cloudNetwork->COMGETTER(NetworkName)(networkName.asOutParam());
        RTPrintf(List::tr("Name:            %ls\n"), networkName.raw());
        // Guid interfaceGuid;
        // cloudNetwork->COMGETTER(Id)(interfaceGuid.asOutParam());
        // RTPrintf("GUID:        %ls\n\n", Bstr(interfaceGuid.toString()).raw());
        BOOL fEnabled = FALSE;
        cloudNetwork->COMGETTER(Enabled)(&fEnabled);
        RTPrintf(List::tr("State:           %s\n"), fEnabled ? List::tr("Enabled") : List::tr("Disabled"));

        Bstr Provider;
        cloudNetwork->COMGETTER(Provider)(Provider.asOutParam());
        RTPrintf(List::tr("CloudProvider:   %ls\n"), Provider.raw());
        Bstr Profile;
        cloudNetwork->COMGETTER(Profile)(Profile.asOutParam());
        RTPrintf(List::tr("CloudProfile:    %ls\n"), Profile.raw());
        Bstr NetworkId;
        cloudNetwork->COMGETTER(NetworkId)(NetworkId.asOutParam());
        RTPrintf(List::tr("CloudNetworkId:  %ls\n"), NetworkId.raw());
        Bstr netName = BstrFmt("cloud-%ls", networkName.raw());
        RTPrintf(List::tr("VBoxNetworkName: %ls\n\n"), netName.raw());
    }
    return S_OK;
}
#endif /* VBOX_WITH_CLOUD_NET */


/**
 * List host information.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listHostInfo(const ComPtr<IVirtualBox> pVirtualBox)
{
    static struct
    {
        ProcessorFeature_T feature;
        const char *pszName;
    } features[]
    =
    {
        { ProcessorFeature_HWVirtEx,     List::tr("HW virtualization") },
        { ProcessorFeature_PAE,          "PAE" },
        { ProcessorFeature_LongMode,     List::tr("long mode") },
        { ProcessorFeature_NestedPaging, List::tr("nested paging") },
        { ProcessorFeature_UnrestrictedGuest, List::tr("unrestricted guest") },
        { ProcessorFeature_NestedHWVirt, List::tr("nested HW virtualization") },
        { ProcessorFeature_VirtVmsaveVmload, List::tr("virt. vmsave/vmload") },
    };
    HRESULT hrc;
    ComPtr<IHost> Host;
    CHECK_ERROR(pVirtualBox, COMGETTER(Host)(Host.asOutParam()));

    RTPrintf(List::tr("Host Information:\n\n"));

    LONG64      u64UtcTime = 0;
    CHECK_ERROR(Host, COMGETTER(UTCTime)(&u64UtcTime));
    RTTIMESPEC  timeSpec;
    char        szTime[32];
    RTPrintf(List::tr("Host time: %s\n"), RTTimeSpecToString(RTTimeSpecSetMilli(&timeSpec, u64UtcTime), szTime, sizeof(szTime)));

    ULONG processorOnlineCount = 0;
    CHECK_ERROR(Host, COMGETTER(ProcessorOnlineCount)(&processorOnlineCount));
    RTPrintf(List::tr("Processor online count: %lu\n"), processorOnlineCount);
    ULONG processorCount = 0;
    CHECK_ERROR(Host, COMGETTER(ProcessorCount)(&processorCount));
    RTPrintf(List::tr("Processor count: %lu\n"), processorCount);
    ULONG processorOnlineCoreCount = 0;
    CHECK_ERROR(Host, COMGETTER(ProcessorOnlineCoreCount)(&processorOnlineCoreCount));
    RTPrintf(List::tr("Processor online core count: %lu\n"), processorOnlineCoreCount);
    ULONG processorCoreCount = 0;
    CHECK_ERROR(Host, COMGETTER(ProcessorCoreCount)(&processorCoreCount));
    RTPrintf(List::tr("Processor core count: %lu\n"), processorCoreCount);
    for (unsigned i = 0; i < RT_ELEMENTS(features); i++)
    {
        BOOL supported;
        CHECK_ERROR(Host, GetProcessorFeature(features[i].feature, &supported));
        RTPrintf(List::tr("Processor supports %s: %s\n"), features[i].pszName, supported ? List::tr("yes") : List::tr("no"));
    }
    for (ULONG i = 0; i < processorCount; i++)
    {
        ULONG processorSpeed = 0;
        CHECK_ERROR(Host, GetProcessorSpeed(i, &processorSpeed));
        if (processorSpeed)
            RTPrintf(List::tr("Processor#%u speed: %lu MHz\n"), i, processorSpeed);
        else
            RTPrintf(List::tr("Processor#%u speed: unknown\n"), i);
        Bstr processorDescription;
        CHECK_ERROR(Host, GetProcessorDescription(i, processorDescription.asOutParam()));
        RTPrintf(List::tr("Processor#%u description: %ls\n"), i, processorDescription.raw());
    }

    ULONG memorySize = 0;
    CHECK_ERROR(Host, COMGETTER(MemorySize)(&memorySize));
    RTPrintf(List::tr("Memory size: %lu MByte\n", "", memorySize), memorySize);

    ULONG memoryAvailable = 0;
    CHECK_ERROR(Host, COMGETTER(MemoryAvailable)(&memoryAvailable));
    RTPrintf(List::tr("Memory available: %lu MByte\n", "", memoryAvailable), memoryAvailable);

    Bstr operatingSystem;
    CHECK_ERROR(Host, COMGETTER(OperatingSystem)(operatingSystem.asOutParam()));
    RTPrintf(List::tr("Operating system: %ls\n"), operatingSystem.raw());

    Bstr oSVersion;
    CHECK_ERROR(Host, COMGETTER(OSVersion)(oSVersion.asOutParam()));
    RTPrintf(List::tr("Operating system version: %ls\n"), oSVersion.raw());
    return hrc;
}


/**
 * List media information.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 * @param   aMedia              Medium objects to list information for.
 * @param   pszParentUUIDStr    String with the parent UUID string (or "base").
 * @param   fOptLong            Long (@c true) or short list format.
 */
static HRESULT listMedia(const ComPtr<IVirtualBox> pVirtualBox,
                         const com::SafeIfaceArray<IMedium> &aMedia,
                         const char *pszParentUUIDStr,
                         bool fOptLong)
{
    HRESULT hrc = S_OK;
    for (size_t i = 0; i < aMedia.size(); ++i)
    {
        ComPtr<IMedium> pMedium = aMedia[i];

        hrc = showMediumInfo(pVirtualBox, pMedium, pszParentUUIDStr, fOptLong);

        RTPrintf("\n");

        com::SafeIfaceArray<IMedium> children;
        CHECK_ERROR(pMedium, COMGETTER(Children)(ComSafeArrayAsOutParam(children)));
        if (children.size() > 0)
        {
            Bstr uuid;
            pMedium->COMGETTER(Id)(uuid.asOutParam());

            // depth first listing of child media
            hrc = listMedia(pVirtualBox, children, Utf8Str(uuid).c_str(), fOptLong);
        }
    }

    return hrc;
}


/**
 * List virtual image backends.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listHddBackends(const ComPtr<IVirtualBox> pVirtualBox)
{
    HRESULT hrc;
    ComPtr<ISystemProperties> systemProperties;
    CHECK_ERROR(pVirtualBox, COMGETTER(SystemProperties)(systemProperties.asOutParam()));
    com::SafeIfaceArray<IMediumFormat> mediumFormats;
    CHECK_ERROR(systemProperties, COMGETTER(MediumFormats)(ComSafeArrayAsOutParam(mediumFormats)));

    RTPrintf(List::tr("Supported hard disk backends:\n\n"));
    for (size_t i = 0; i < mediumFormats.size(); ++i)
    {
        /* General information */
        Bstr id;
        CHECK_ERROR(mediumFormats[i], COMGETTER(Id)(id.asOutParam()));

        Bstr description;
        CHECK_ERROR(mediumFormats[i],
                    COMGETTER(Name)(description.asOutParam()));

        ULONG caps = 0;
        com::SafeArray <MediumFormatCapabilities_T> mediumFormatCap;
        CHECK_ERROR(mediumFormats[i],
                    COMGETTER(Capabilities)(ComSafeArrayAsOutParam(mediumFormatCap)));
        for (ULONG j = 0; j < mediumFormatCap.size(); j++)
            caps |= mediumFormatCap[j];


        RTPrintf(List::tr("Backend %u: id='%ls' description='%ls' capabilities=%#06x extensions='"),
                i, id.raw(), description.raw(), caps);

        /* File extensions */
        com::SafeArray<BSTR> fileExtensions;
        com::SafeArray<DeviceType_T> deviceTypes;
        CHECK_ERROR(mediumFormats[i],
                    DescribeFileExtensions(ComSafeArrayAsOutParam(fileExtensions), ComSafeArrayAsOutParam(deviceTypes)));
        for (size_t j = 0; j < fileExtensions.size(); ++j)
        {
            RTPrintf("%ls (%s)", Bstr(fileExtensions[j]).raw(), getDeviceTypeText(deviceTypes[j]));
            if (j != fileExtensions.size()-1)
                RTPrintf(",");
        }
        RTPrintf("'");

        /* Configuration keys */
        com::SafeArray<BSTR> propertyNames;
        com::SafeArray<BSTR> propertyDescriptions;
        com::SafeArray<DataType_T> propertyTypes;
        com::SafeArray<ULONG> propertyFlags;
        com::SafeArray<BSTR> propertyDefaults;
        CHECK_ERROR(mediumFormats[i],
                    DescribeProperties(ComSafeArrayAsOutParam(propertyNames),
                                        ComSafeArrayAsOutParam(propertyDescriptions),
                                        ComSafeArrayAsOutParam(propertyTypes),
                                        ComSafeArrayAsOutParam(propertyFlags),
                                        ComSafeArrayAsOutParam(propertyDefaults)));

        RTPrintf(List::tr(" properties=("));
        if (propertyNames.size() > 0)
        {
            for (size_t j = 0; j < propertyNames.size(); ++j)
            {
                RTPrintf(List::tr("\n  name='%ls' desc='%ls' type="),
                        Bstr(propertyNames[j]).raw(), Bstr(propertyDescriptions[j]).raw());
                switch (propertyTypes[j])
                {
                    case DataType_Int32: RTPrintf(List::tr("int")); break;
                    case DataType_Int8: RTPrintf(List::tr("byte")); break;
                    case DataType_String: RTPrintf(List::tr("string")); break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
                    case DataType_32BitHack: break; /* Shut up compiler warnings. */
#endif
                }
                RTPrintf(List::tr(" flags=%#04x"), propertyFlags[j]);
                RTPrintf(List::tr(" default='%ls'"), Bstr(propertyDefaults[j]).raw());
                if (j != propertyNames.size()-1)
                    RTPrintf(", ");
            }
        }
        RTPrintf(")\n");
    }
    return hrc;
}


/**
 * List USB devices attached to the host.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listUsbHost(const ComPtr<IVirtualBox> &pVirtualBox)
{
    HRESULT hrc;
    ComPtr<IHost> Host;
    CHECK_ERROR_RET(pVirtualBox, COMGETTER(Host)(Host.asOutParam()), 1);

    SafeIfaceArray<IHostUSBDevice> CollPtr;
    CHECK_ERROR_RET(Host, COMGETTER(USBDevices)(ComSafeArrayAsOutParam(CollPtr)), 1);

    RTPrintf(List::tr("Host USB Devices:\n\n"));

    if (CollPtr.size() == 0)
    {
        RTPrintf(List::tr("<none>\n\n"));
    }
    else
    {
        for (size_t i = 0; i < CollPtr.size(); ++i)
        {
            ComPtr<IHostUSBDevice> dev = CollPtr[i];

            /* Query info. */
            Bstr id;
            CHECK_ERROR_RET(dev, COMGETTER(Id)(id.asOutParam()), 1);
            USHORT usVendorId;
            CHECK_ERROR_RET(dev, COMGETTER(VendorId)(&usVendorId), 1);
            USHORT usProductId;
            CHECK_ERROR_RET(dev, COMGETTER(ProductId)(&usProductId), 1);
            USHORT bcdRevision;
            CHECK_ERROR_RET(dev, COMGETTER(Revision)(&bcdRevision), 1);
            USHORT usPort;
            CHECK_ERROR_RET(dev, COMGETTER(Port)(&usPort), 1);
            USHORT usVersion;
            CHECK_ERROR_RET(dev, COMGETTER(Version)(&usVersion), 1);
            USBConnectionSpeed_T enmSpeed;
            CHECK_ERROR_RET(dev, COMGETTER(Speed)(&enmSpeed), 1);

            RTPrintf(List::tr(
                       "UUID:               %s\n"
                       "VendorId:           %#06x (%04X)\n"
                       "ProductId:          %#06x (%04X)\n"
                       "Revision:           %u.%u (%02u%02u)\n"
                       "Port:               %u\n"),
                     Utf8Str(id).c_str(),
                     usVendorId, usVendorId, usProductId, usProductId,
                     bcdRevision >> 8, bcdRevision & 0xff,
                     bcdRevision >> 8, bcdRevision & 0xff,
                     usPort);

            const char *pszSpeed = "?";
            switch (enmSpeed)
            {
                case USBConnectionSpeed_Low:
                    pszSpeed = List::tr("Low");
                    break;
                case USBConnectionSpeed_Full:
                    pszSpeed = List::tr("Full");
                    break;
                case USBConnectionSpeed_High:
                    pszSpeed = List::tr("High");
                    break;
                case USBConnectionSpeed_Super:
                    pszSpeed = List::tr("Super");
                    break;
                case USBConnectionSpeed_SuperPlus:
                    pszSpeed = List::tr("SuperPlus");
                    break;
                default:
                    ASSERT(false);
                    break;
            }

            RTPrintf(List::tr("USB version/speed:  %u/%s\n"), usVersion, pszSpeed);

            /* optional stuff. */
            SafeArray<BSTR> CollDevInfo;
            Bstr bstr;
            CHECK_ERROR_RET(dev, COMGETTER(DeviceInfo)(ComSafeArrayAsOutParam(CollDevInfo)), 1);
            if (CollDevInfo.size() >= 1)
                bstr = Bstr(CollDevInfo[0]);
            if (!bstr.isEmpty())
                RTPrintf(List::tr("Manufacturer:       %ls\n"), bstr.raw());
            if (CollDevInfo.size() >= 2)
                bstr = Bstr(CollDevInfo[1]);
            if (!bstr.isEmpty())
                RTPrintf(List::tr("Product:            %ls\n"), bstr.raw());
            CHECK_ERROR_RET(dev, COMGETTER(SerialNumber)(bstr.asOutParam()), 1);
            if (!bstr.isEmpty())
                RTPrintf(List::tr("SerialNumber:       %ls\n"), bstr.raw());
            CHECK_ERROR_RET(dev, COMGETTER(Address)(bstr.asOutParam()), 1);
            if (!bstr.isEmpty())
                RTPrintf(List::tr("Address:            %ls\n"), bstr.raw());
            CHECK_ERROR_RET(dev, COMGETTER(PortPath)(bstr.asOutParam()), 1);
            if (!bstr.isEmpty())
                RTPrintf(List::tr("Port path:          %ls\n"), bstr.raw());

            /* current state  */
            USBDeviceState_T state;
            CHECK_ERROR_RET(dev, COMGETTER(State)(&state), 1);
            const char *pszState = "?";
            switch (state)
            {
                case USBDeviceState_NotSupported:
                    pszState = List::tr("Not supported");
                    break;
                case USBDeviceState_Unavailable:
                    pszState = List::tr("Unavailable");
                    break;
                case USBDeviceState_Busy:
                    pszState = List::tr("Busy");
                    break;
                case USBDeviceState_Available:
                    pszState = List::tr("Available");
                    break;
                case USBDeviceState_Held:
                    pszState = List::tr("Held");
                    break;
                case USBDeviceState_Captured:
                    pszState = List::tr("Captured");
                    break;
                default:
                    ASSERT(false);
                    break;
            }
            RTPrintf(List::tr("Current State:      %s\n\n"), pszState);
        }
    }
    return hrc;
}


/**
 * List USB filters.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listUsbFilters(const ComPtr<IVirtualBox> &pVirtualBox)
{
    HRESULT hrc;

    RTPrintf(List::tr("Global USB Device Filters:\n\n"));

    ComPtr<IHost> host;
    CHECK_ERROR_RET(pVirtualBox, COMGETTER(Host)(host.asOutParam()), 1);

    SafeIfaceArray<IHostUSBDeviceFilter> coll;
    CHECK_ERROR_RET(host, COMGETTER(USBDeviceFilters)(ComSafeArrayAsOutParam(coll)), 1);

    if (coll.size() == 0)
    {
        RTPrintf(List::tr("<none>\n\n"));
    }
    else
    {
        for (size_t index = 0; index < coll.size(); ++index)
        {
            ComPtr<IHostUSBDeviceFilter> flt = coll[index];

            /* Query info. */

            RTPrintf(List::tr("Index:            %zu\n"), index);

            BOOL active = FALSE;
            CHECK_ERROR_RET(flt, COMGETTER(Active)(&active), 1);
            RTPrintf(List::tr("Active:           %s\n"), active ? List::tr("yes") : List::tr("no"));

            USBDeviceFilterAction_T action;
            CHECK_ERROR_RET(flt, COMGETTER(Action)(&action), 1);
            const char *pszAction = List::tr("<invalid>");
            switch (action)
            {
                case USBDeviceFilterAction_Ignore:
                    pszAction = List::tr("Ignore");
                    break;
                case USBDeviceFilterAction_Hold:
                    pszAction = List::tr("Hold");
                    break;
                default:
                    break;
            }
            RTPrintf(List::tr("Action:           %s\n"), pszAction);

            Bstr bstr;
            CHECK_ERROR_RET(flt, COMGETTER(Name)(bstr.asOutParam()), 1);
            RTPrintf(List::tr("Name:             %ls\n"), bstr.raw());
            CHECK_ERROR_RET(flt, COMGETTER(VendorId)(bstr.asOutParam()), 1);
            RTPrintf(List::tr("VendorId:         %ls\n"), bstr.raw());
            CHECK_ERROR_RET(flt, COMGETTER(ProductId)(bstr.asOutParam()), 1);
            RTPrintf(List::tr("ProductId:        %ls\n"), bstr.raw());
            CHECK_ERROR_RET(flt, COMGETTER(Revision)(bstr.asOutParam()), 1);
            RTPrintf(List::tr("Revision:         %ls\n"), bstr.raw());
            CHECK_ERROR_RET(flt, COMGETTER(Manufacturer)(bstr.asOutParam()), 1);
            RTPrintf(List::tr("Manufacturer:     %ls\n"), bstr.raw());
            CHECK_ERROR_RET(flt, COMGETTER(Product)(bstr.asOutParam()), 1);
            RTPrintf(List::tr("Product:          %ls\n"), bstr.raw());
            CHECK_ERROR_RET(flt, COMGETTER(SerialNumber)(bstr.asOutParam()), 1);
            RTPrintf(List::tr("Serial Number:    %ls\n"), bstr.raw());
            CHECK_ERROR_RET(flt, COMGETTER(Port)(bstr.asOutParam()), 1);
            RTPrintf(List::tr("Port:             %ls\n\n"), bstr.raw());
        }
    }
    return hrc;
}

/**
 * Returns the chipset type as a string.
 *
 * @return Chipset type as a string.
 * @param  enmType               Chipset type to convert.
 */
static const char *chipsetTypeToStr(ChipsetType_T enmType)
{
    switch (enmType)
    {
        case ChipsetType_PIIX3:        return "PIIX3";
        case ChipsetType_ICH9:         return "ICH9";
        case ChipsetType_ARMv8Virtual: return "ARMv8Virtual";
        case ChipsetType_Null:
        default:
            break;
    }

    return "<Unknown>";
}

/**
 * Returns a platform architecture as a string.
 *
 * @return Platform architecture as a string.
 * @param  enmArch               Platform architecture to convert.
 */
static const char *platformArchitectureToStr(PlatformArchitecture_T enmArch)
{
    switch (enmArch)
    {
        case PlatformArchitecture_x86: return "x86";
        case PlatformArchitecture_ARM: return "ARMv8";
        default:
            break;
    }

    return "<Unknown>";
}

/**
 * Returns the platform architecture for a given string.
 *
 * @return Platform architecture, or PlatformArchitecture_None if not found.
 * @param  pszPlatform           Platform architecture to convert.
 */
static PlatformArchitecture_T platformArchitectureToStr(const char *pszPlatform)
{
    if (   !RTStrICmp(pszPlatform, "x86")
        || !RTStrICmp(pszPlatform, "x86_64")
        || !RTStrICmp(pszPlatform, "ia32")
        || !RTStrICmp(pszPlatform, "amd64")
        || !RTStrICmp(pszPlatform, "intel"))
        return PlatformArchitecture_x86;
    else if (   !RTStrICmp(pszPlatform, "arm")
             || !RTStrICmp(pszPlatform, "armv8"))
        return PlatformArchitecture_ARM;
    return PlatformArchitecture_None;
}

/** @todo r=andy Make use of SHOW_ULONG_PROP and friends like in VBoxManageInfo to have a more uniform / prettier output.
 *               Use nesting (as padding / tabs). */

/**
 * List chipset properties.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listPlatformChipsetProperties(const ComPtr<IPlatformProperties> &pPlatformProperties, ChipsetType_T enmChipsetType)
{
    const char *pszChipset = chipsetTypeToStr(enmChipsetType);
    AssertPtrReturn(pszChipset, E_INVALIDARG);

    /* Note: Keep the chipset name within the description -- makes it easier to grep for specific chipsts manually. */
    ULONG ulValue;
    pPlatformProperties->GetMaxNetworkAdapters(enmChipsetType, &ulValue);
    RTPrintf(List::tr("Maximum %s Network Adapter count:   %u\n"), pszChipset, ulValue);
    pPlatformProperties->GetMaxInstancesOfStorageBus(enmChipsetType, StorageBus_IDE, &ulValue);
    RTPrintf(List::tr("Maximum %s IDE Controllers:   %u\n"), pszChipset, ulValue);
    pPlatformProperties->GetMaxInstancesOfStorageBus(enmChipsetType, StorageBus_SATA, &ulValue);
    RTPrintf(List::tr("Maximum %s SATA Controllers:  %u\n"), pszChipset, ulValue);
    pPlatformProperties->GetMaxInstancesOfStorageBus(enmChipsetType, StorageBus_SCSI, &ulValue);
    RTPrintf(List::tr("Maximum %s SCSI Controllers:  %u\n"), pszChipset, ulValue);
    pPlatformProperties->GetMaxInstancesOfStorageBus(enmChipsetType, StorageBus_SAS, &ulValue);
    RTPrintf(List::tr("Maximum %s SAS Controllers:   %u\n"), pszChipset, ulValue);
    pPlatformProperties->GetMaxInstancesOfStorageBus(enmChipsetType, StorageBus_PCIe, &ulValue);
    RTPrintf(List::tr("Maximum %s NVMe Controllers:  %u\n"), pszChipset, ulValue);
    pPlatformProperties->GetMaxInstancesOfStorageBus(enmChipsetType, StorageBus_VirtioSCSI, &ulValue);
    RTPrintf(List::tr("Maximum %s virtio-scsi Controllers:  %u\n"), pszChipset, ulValue);
    pPlatformProperties->GetMaxInstancesOfStorageBus(enmChipsetType, StorageBus_Floppy, &ulValue);
    RTPrintf(List::tr("Maximum %s Floppy Controllers:%u\n"), pszChipset, ulValue);

    return S_OK;
}

/**
 * Shows a single guest OS type.
 *
 * @returns HRESULT
 * @param   ptrGuestOS          Guest OS type to show.
 * @param   fLong               Set to @true to show the guest OS type information in a not-so-compact mode.
 */
static HRESULT showGuestOSType(ComPtr<IGuestOSType> ptrGuestOS, bool fLong)
{
    PlatformArchitecture_T enmPlatformArch = (PlatformArchitecture_T)PlatformArchitecture_None;
    ptrGuestOS->COMGETTER(PlatformArchitecture)(&enmPlatformArch);
    Bstr guestId;
    Bstr guestDescription;
    ptrGuestOS->COMGETTER(Description)(guestDescription.asOutParam());
    ptrGuestOS->COMGETTER(Id)(guestId.asOutParam());
    Bstr familyId;
    Bstr familyDescription;
    ptrGuestOS->COMGETTER(FamilyId)(familyId.asOutParam());
    ptrGuestOS->COMGETTER(FamilyDescription)(familyDescription.asOutParam());
    Bstr guestOSSubtype;
    ptrGuestOS->COMGETTER(Subtype)(guestOSSubtype.asOutParam());
    BOOL fIs64Bit;
    ptrGuestOS->COMGETTER(Is64Bit)(&fIs64Bit);

    if (fLong)
    {
        RTPrintf(         "ID:               %ls\n", guestId.raw());
        RTPrintf(List::tr("Description:      %ls\n"), guestDescription.raw());
        RTPrintf(List::tr("Family ID:        %ls\n"), familyId.raw());
        RTPrintf(List::tr("Family Desc:      %ls\n"), familyDescription.raw());
        if (guestOSSubtype.isNotEmpty())
            RTPrintf(List::tr("OS Subtype:       %ls\n"), guestOSSubtype.raw());
        RTPrintf(List::tr("Architecture:     %s\n"), platformArchitectureToStr(enmPlatformArch));
        RTPrintf(List::tr("64 bit:           %RTbool\n"), fIs64Bit);
    }
    else
    {
        RTPrintf(         "ID / Description: %ls -- %ls\n", guestId.raw(), guestDescription.raw());
        if (guestOSSubtype.isNotEmpty())
            RTPrintf(List::tr("Family:           %ls / %ls (%ls)\n"),
                     familyId.raw(), guestOSSubtype.raw(), familyDescription.raw());
        else
            RTPrintf(List::tr("Family:           %ls (%ls)\n"), familyId.raw(), familyDescription.raw());
        RTPrintf(List::tr("Architecture:     %s%s\n"), platformArchitectureToStr(enmPlatformArch),
                                                      fIs64Bit ? " (64-bit)" : "");
    }
    RTPrintf("\n");

    return S_OK;
}

/**
 * Lists guest OS types.
 *
 * @returns HRESULT
 * @param   aGuestOSTypes           Reference to guest OS types to list.
 * @param   fLong                   Set to @true to list the OS types in a not-so-compact mode.
 * @param   fSorted                 Set to @true to list the OS types in a sorted manner (by guest OS type ID).
 * @param   enmFilterByPlatformArch Filters the output by the given platform architecture, or shows all supported guest OS types
 *                                  if PlatformArchitecture_None is specified.
 */
static HRESULT listGuestOSTypes(const com::SafeIfaceArray<IGuestOSType> &aGuestOSTypes, bool fLong, bool fSorted,
                                PlatformArchitecture_T enmFilterByPlatformArch)
{
    RTPrintf(List::tr("Supported guest OS types%s:\n\n"),
                      enmFilterByPlatformArch != PlatformArchitecture_None ? " (filtered)" : "");

/** Filters the guest OS type output by skipping the current iteration. */
#define FILTER_OUTPUT(a_GuestOSType) \
    PlatformArchitecture_T enmPlatformArch = (PlatformArchitecture_T)PlatformArchitecture_None; \
    ptrGuestOS->COMGETTER(PlatformArchitecture)(&enmPlatformArch); \
    if (   enmFilterByPlatformArch != PlatformArchitecture_None \
        && enmFilterByPlatformArch != enmPlatformArch) \
        continue;

    if (fSorted)
    {
        std::vector<std::pair<com::Bstr, IGuestOSType *> > sortedGuestOSTypes;
        for (size_t i = 0; i < aGuestOSTypes.size(); ++i)
        {
            ComPtr<IGuestOSType> ptrGuestOS = aGuestOSTypes[i];
            FILTER_OUTPUT(ptrGuestOS);

            /* We sort by guest type ID. */
            Bstr guestId;
            ptrGuestOS->COMGETTER(Id)(guestId.asOutParam());
            sortedGuestOSTypes.push_back(std::pair<com::Bstr, IGuestOSType *>(guestId, ptrGuestOS));
        }

        std::sort(sortedGuestOSTypes.begin(), sortedGuestOSTypes.end());
        for (size_t i = 0; i < sortedGuestOSTypes.size(); ++i)
            showGuestOSType(sortedGuestOSTypes[i].second, fLong);
    }
    else
    {
        for (size_t i = 0; i < aGuestOSTypes.size(); ++i)
        {
            ComPtr<IGuestOSType> ptrGuestOS = aGuestOSTypes[i];
            FILTER_OUTPUT(ptrGuestOS);

            showGuestOSType(ptrGuestOS, fLong);
        }
    }

#undef FILTER_OUTPUT

    return S_OK;
}

static HRESULT listPlatformProperties(const ComPtr<IPlatformProperties> &platformProperties)
{
    ULONG ulValue;
    platformProperties->COMGETTER(SerialPortCount)(&ulValue);
    RTPrintf(List::tr("Maximum Serial Port count:              %u\n"), ulValue);
    platformProperties->COMGETTER(ParallelPortCount)(&ulValue);
    RTPrintf(List::tr("Maximum Parallel Port count:            %u\n"), ulValue);
    platformProperties->COMGETTER(MaxBootPosition)(&ulValue);
    RTPrintf(List::tr("Maximum Boot Position:                  %u\n"), ulValue);
    platformProperties->GetMaxPortCountForStorageBus(StorageBus_Floppy, &ulValue);
    RTPrintf(List::tr("Maximum Floppy Port count:              %u\n"), ulValue);
    platformProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_Floppy, &ulValue);
    RTPrintf(List::tr("Maximum Floppy Devices per Port:        %u\n"), ulValue);
    platformProperties->GetMaxPortCountForStorageBus(StorageBus_VirtioSCSI, &ulValue);
    RTPrintf(List::tr("Maximum virtio-scsi Port count:         %u\n"), ulValue);
    platformProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_VirtioSCSI, &ulValue);
    RTPrintf(List::tr("Maximum virtio-scsi Devices per Port:   %u\n"), ulValue);
    platformProperties->GetMaxPortCountForStorageBus(StorageBus_IDE, &ulValue);
    RTPrintf(List::tr("Maximum IDE Port count:                 %u\n"), ulValue);
    platformProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_IDE, &ulValue);
    RTPrintf(List::tr("Maximum IDE Devices per port:           %u\n"), ulValue);
    platformProperties->GetMaxPortCountForStorageBus(StorageBus_SATA, &ulValue);
    RTPrintf(List::tr("Maximum SATA Port count:                %u\n"), ulValue);
    platformProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_SATA, &ulValue);
    RTPrintf(List::tr("Maximum SATA Device per port:          %u\n"), ulValue);
    platformProperties->GetMaxPortCountForStorageBus(StorageBus_SCSI, &ulValue);
    RTPrintf(List::tr("Maximum SCSI Port count:                %u\n"), ulValue);
    platformProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_SCSI, &ulValue);
    RTPrintf(List::tr("Maximum SCSI Devices per port:          %u\n"), ulValue);
    platformProperties->GetMaxPortCountForStorageBus(StorageBus_SAS, &ulValue);
    RTPrintf(List::tr("Maximum SAS Port count:                 %u\n"), ulValue);
    platformProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_SAS, &ulValue);
    RTPrintf(List::tr("Maximum SAS Devices per Port:           %u\n"), ulValue);
    platformProperties->GetMaxPortCountForStorageBus(StorageBus_PCIe, &ulValue);
    RTPrintf(List::tr("Maximum NVMe Port count:                %u\n"), ulValue);
    platformProperties->GetMaxDevicesPerPortForStorageBus(StorageBus_PCIe, &ulValue);
    RTPrintf(List::tr("Maximum NVMe Devices per Port:          %u\n"), ulValue);

    SafeArray <ChipsetType_T> saChipset;
    platformProperties->COMGETTER(SupportedChipsetTypes(ComSafeArrayAsOutParam(saChipset)));

    RTPrintf(List::tr("Supported chipsets:                     "));
    for (size_t i = 0; i < saChipset.size(); i++)
    {
        if (i > 0)
            RTPrintf(", ");
        RTPrintf("%s", chipsetTypeToStr(saChipset[i]));
    }
    RTPrintf("\n");

    for (size_t i = 0; i < saChipset.size(); i++)
    {
        if (i > 0)
            RTPrintf("\n");
        RTPrintf(List::tr("%s chipset properties:\n"), chipsetTypeToStr(saChipset[i]));
        listPlatformChipsetProperties(platformProperties, saChipset[i]);
    }

    return S_OK;
}

/**
 * List system properties.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listSystemProperties(const ComPtr<IVirtualBox> &pVirtualBox)
{
    ComPtr<ISystemProperties> systemProperties;
    CHECK_ERROR2I_RET(pVirtualBox, COMGETTER(SystemProperties)(systemProperties.asOutParam()), hrcCheck);

    ComPtr<IPlatformProperties> hostPlatformProperties;
    CHECK_ERROR2I_RET(systemProperties, COMGETTER(Platform)(hostPlatformProperties.asOutParam()), hrcCheck);

    Bstr str;
    ULONG ulValue;
    LONG64 i64Value;
    BOOL fValue;
    const char *psz;

    pVirtualBox->COMGETTER(APIVersion)(str.asOutParam());
    RTPrintf(List::tr("API version:                     %ls\n"), str.raw());

    systemProperties->COMGETTER(MinGuestRAM)(&ulValue);
    RTPrintf(List::tr("Minimum guest RAM size:          %u Megabytes\n", "", ulValue), ulValue);
    systemProperties->COMGETTER(MaxGuestRAM)(&ulValue);
    RTPrintf(List::tr("Maximum guest RAM size:          %u Megabytes\n", "", ulValue), ulValue);
    systemProperties->COMGETTER(MinGuestVRAM)(&ulValue);
    RTPrintf(List::tr("Minimum video RAM size:          %u Megabytes\n", "", ulValue), ulValue);
    systemProperties->COMGETTER(MaxGuestVRAM)(&ulValue);
    RTPrintf(List::tr("Maximum video RAM size:          %u Megabytes\n", "", ulValue), ulValue);
    systemProperties->COMGETTER(MaxGuestMonitors)(&ulValue);
    RTPrintf(List::tr("Maximum guest monitor count:     %u\n"), ulValue);
    systemProperties->COMGETTER(MinGuestCPUCount)(&ulValue);
    RTPrintf(List::tr("Minimum guest CPU count:         %u\n"), ulValue);
    systemProperties->COMGETTER(MaxGuestCPUCount)(&ulValue);
    RTPrintf(List::tr("Maximum guest CPU count:         %u\n"), ulValue);
    systemProperties->COMGETTER(InfoVDSize)(&i64Value);
    RTPrintf(List::tr("Virtual disk limit (info):       %lld Bytes\n", "" , i64Value), i64Value);

#if 0
    systemProperties->GetFreeDiskSpaceWarning(&i64Value);
    RTPrintf(List::tr("Free disk space warning at:      %u Bytes\n", "", i64Value), i64Value);
    systemProperties->GetFreeDiskSpacePercentWarning(&ulValue);
    RTPrintf(List::tr("Free disk space warning at:      %u %%\n"), ulValue);
    systemProperties->GetFreeDiskSpaceError(&i64Value);
    RTPrintf(List::tr("Free disk space error at:        %u Bytes\n", "", i64Value), i64Value);
    systemProperties->GetFreeDiskSpacePercentError(&ulValue);
    RTPrintf(List::tr("Free disk space error at:        %u %%\n"), ulValue);
#endif
    systemProperties->COMGETTER(DefaultMachineFolder)(str.asOutParam());
    RTPrintf(List::tr("Default machine folder:          %ls\n"), str.raw());
    hostPlatformProperties->COMGETTER(RawModeSupported)(&fValue);
    RTPrintf(List::tr("Raw-mode Supported:              %s\n"), fValue ? List::tr("yes") : List::tr("no"));
    hostPlatformProperties->COMGETTER(ExclusiveHwVirt)(&fValue);
    RTPrintf(List::tr("Exclusive HW virtualization use: %s\n"), fValue ? List::tr("on") : List::tr("off"));
    systemProperties->COMGETTER(DefaultHardDiskFormat)(str.asOutParam());
    RTPrintf(List::tr("Default hard disk format:        %ls\n"), str.raw());
    systemProperties->COMGETTER(VRDEAuthLibrary)(str.asOutParam());
    RTPrintf(List::tr("VRDE auth library:               %ls\n"), str.raw());
    systemProperties->COMGETTER(WebServiceAuthLibrary)(str.asOutParam());
    RTPrintf(List::tr("Webservice auth. library:        %ls\n"), str.raw());
    systemProperties->COMGETTER(DefaultVRDEExtPack)(str.asOutParam());
    RTPrintf(List::tr("Remote desktop ExtPack:          %ls\n"), str.raw());
    systemProperties->COMGETTER(DefaultCryptoExtPack)(str.asOutParam());
    RTPrintf(List::tr("VM encryption ExtPack:           %ls\n"), str.raw());
    systemProperties->COMGETTER(LogHistoryCount)(&ulValue);
    RTPrintf(List::tr("Log history count:               %u\n"), ulValue);
    systemProperties->COMGETTER(DefaultFrontend)(str.asOutParam());
    RTPrintf(List::tr("Default frontend:                %ls\n"), str.raw());
    AudioDriverType_T enmAudio;
    systemProperties->COMGETTER(DefaultAudioDriver)(&enmAudio);
    switch (enmAudio)
    {
        case AudioDriverType_Default:       psz = List::tr("Default");     break;
        case AudioDriverType_Null:          psz = List::tr("Null");        break;
        case AudioDriverType_OSS:           psz = "OSS";                   break;
        case AudioDriverType_ALSA:          psz = "ALSA";                  break;
        case AudioDriverType_Pulse:         psz = "PulseAudio";            break;
        case AudioDriverType_WinMM:         psz = "WinMM";                 break;
        case AudioDriverType_DirectSound:   psz = "DirectSound";           break;
        case AudioDriverType_WAS:           psz = "Windows Audio Session"; break;
        case AudioDriverType_CoreAudio:     psz = "CoreAudio";             break;
        case AudioDriverType_SolAudio:      psz = "SolAudio";              break;
        case AudioDriverType_MMPM:          psz = "MMPM";                  break;
        default:                            psz = List::tr("Unknown");
    }
    RTPrintf(List::tr("Default audio driver:            %s\n"), psz);
    systemProperties->COMGETTER(AutostartDatabasePath)(str.asOutParam());
    RTPrintf(List::tr("Autostart database path:         %ls\n"), str.raw());
    systemProperties->COMGETTER(DefaultAdditionsISO)(str.asOutParam());
    RTPrintf(List::tr("Default Guest Additions ISO:     %ls\n"), str.raw());
    systemProperties->COMGETTER(LoggingLevel)(str.asOutParam());
    RTPrintf(List::tr("Logging Level:                   %ls\n"), str.raw());
    ProxyMode_T enmProxyMode = (ProxyMode_T)42;
    systemProperties->COMGETTER(ProxyMode)(&enmProxyMode);
    psz = List::tr("Unknown");
    switch (enmProxyMode)
    {
        case ProxyMode_System:              psz = List::tr("System"); break;
        case ProxyMode_NoProxy:             psz = List::tr("NoProxy"); break;
        case ProxyMode_Manual:              psz = List::tr("Manual"); break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case ProxyMode_32BitHack:           break; /* Shut up compiler warnings. */
#endif
    }
    RTPrintf(List::tr("Proxy Mode:                      %s\n"), psz);
    systemProperties->COMGETTER(ProxyURL)(str.asOutParam());
    RTPrintf(List::tr("Proxy URL:                       %ls\n"), str.raw());
#ifdef VBOX_WITH_MAIN_NLS
    systemProperties->COMGETTER(LanguageId)(str.asOutParam());
    RTPrintf(List::tr("User language:                   %ls\n"), str.raw());
#endif

    RTPrintf("Host platform properties:\n");
    listPlatformProperties(hostPlatformProperties);

    /* Separate host system / platform properties stuff from guest platform properties a bit. */
    RTPrintf("\n");

    SafeArray <PlatformArchitecture_T> saPlatformArch;
    systemProperties->COMGETTER(SupportedPlatformArchitectures(ComSafeArrayAsOutParam(saPlatformArch)));
    RTPrintf("Supported platform architectures: ");
    for (size_t i = 0; i < saPlatformArch.size(); ++i)
    {
        if (i > 0)
            RTPrintf(",");
        RTPrintf(platformArchitectureToStr(saPlatformArch[i]));
    }
    RTPrintf("\n\n");

    for (size_t i = 0; i < saPlatformArch.size(); ++i)
    {
        if (i > 0)
            RTPrintf("\n");
        ComPtr<IPlatformProperties> platformProperties;
        pVirtualBox->GetPlatformProperties(saPlatformArch[i], platformProperties.asOutParam());
        RTPrintf(List::tr("%s platform properties:\n"), platformArchitectureToStr(saPlatformArch[i]));
        listPlatformProperties(platformProperties);
    }

    return S_OK;
}

#ifdef VBOX_WITH_UPDATE_AGENT
static HRESULT listUpdateAgentConfig(ComPtr<IUpdateAgent> ptrUpdateAgent)
{
    BOOL fValue;
    ptrUpdateAgent->COMGETTER(Enabled)(&fValue);
    RTPrintf(List::tr("Enabled:                      %s\n"), fValue ? List::tr("yes") : List::tr("no"));
    ULONG ulValue;
    ptrUpdateAgent->COMGETTER(CheckCount)(&ulValue);
    RTPrintf(List::tr("Check count:                  %u\n"), ulValue);
    ptrUpdateAgent->COMGETTER(CheckFrequency)(&ulValue);
    if (ulValue == 0)
        RTPrintf(List::tr("Check frequency:              never\n"));
    else if (ulValue == 1)
        RTPrintf(List::tr("Check frequency:              every day\n"));
    else
        RTPrintf(List::tr("Check frequency:              every %u days\n", "", ulValue), ulValue);

    Bstr        str;
    const char *psz;
    UpdateChannel_T enmUpdateChannel;
    ptrUpdateAgent->COMGETTER(Channel)(&enmUpdateChannel);
    switch (enmUpdateChannel)
    {
        case UpdateChannel_Stable:
            psz = List::tr("Stable: Maintenance and minor releases within the same major release");
            break;
        case UpdateChannel_All:
            psz = List::tr("All releases: All stable releases, including major versions");
            break;
        case UpdateChannel_WithBetas:
            psz = List::tr("With Betas: All stable and major releases, including beta versions");
            break;
        case UpdateChannel_WithTesting:
            psz = List::tr("With Testing: All stable, major and beta releases, including testing versions");
            break;
        default:
            psz = List::tr("Unset");
            break;
    }
    RTPrintf(List::tr("Channel:                         %s\n"), psz);
    ptrUpdateAgent->COMGETTER(RepositoryURL)(str.asOutParam());
    RTPrintf(List::tr("Repository:                      %ls\n"), str.raw());
    ptrUpdateAgent->COMGETTER(LastCheckDate)(str.asOutParam());
    RTPrintf(List::tr("Last check date:                 %ls\n"), str.raw());

    return S_OK;
}

static HRESULT listUpdateAgents(const ComPtr<IVirtualBox> &pVirtualBox)
{
    ComPtr<IHost> pHost;
    CHECK_ERROR2I_RET(pVirtualBox, COMGETTER(Host)(pHost.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IUpdateAgent> pUpdateHost;
    CHECK_ERROR2I_RET(pHost, COMGETTER(UpdateHost)(pUpdateHost.asOutParam()), RTEXITCODE_FAILURE);
    /** @todo Add other update agents here. */

    return listUpdateAgentConfig(pUpdateHost);
}
#endif  /* VBOX_WITH_UPDATE_AGENT */

/**
 * Helper for listDhcpServers() that shows a DHCP configuration.
 */
static HRESULT showDhcpConfig(ComPtr<IDHCPConfig> ptrConfig)
{
    HRESULT hrcRet = S_OK;

    ULONG   secs = 0;
    CHECK_ERROR2I_STMT(ptrConfig, COMGETTER(MinLeaseTime)(&secs), hrcRet = hrcCheck);
    if (secs == 0)
        RTPrintf(List::tr("    minLeaseTime:     default\n"));
    else
        RTPrintf(List::tr("    minLeaseTime:     %u sec\n"), secs);

    secs = 0;
    CHECK_ERROR2I_STMT(ptrConfig, COMGETTER(DefaultLeaseTime)(&secs), hrcRet = hrcCheck);
    if (secs == 0)
        RTPrintf(List::tr("    defaultLeaseTime: default\n"));
    else
        RTPrintf(List::tr("    defaultLeaseTime: %u sec\n"), secs);

    secs = 0;
    CHECK_ERROR2I_STMT(ptrConfig, COMGETTER(MaxLeaseTime)(&secs), hrcRet = hrcCheck);
    if (secs == 0)
        RTPrintf(List::tr("    maxLeaseTime:     default\n"));
    else
        RTPrintf(List::tr("    maxLeaseTime:     %u sec\n"), secs);

    com::SafeArray<DHCPOption_T>         Options;
    HRESULT hrc;
    CHECK_ERROR2_STMT(hrc, ptrConfig, COMGETTER(ForcedOptions(ComSafeArrayAsOutParam(Options))), hrcRet = hrc);
    if (FAILED(hrc))
        RTPrintf(List::tr("    Forced options:   %Rhrc\n"), hrc);
    else if (Options.size() == 0)
        RTPrintf(List::tr("    Forced options:   None\n"));
    else
    {
        RTPrintf(List::tr("    Forced options:   "));
        for (size_t i = 0; i < Options.size(); i++)
            RTPrintf(i ? ", %u" : "%u", Options[i]);
        RTPrintf("\n");
    }

    CHECK_ERROR2_STMT(hrc, ptrConfig, COMGETTER(SuppressedOptions(ComSafeArrayAsOutParam(Options))), hrcRet = hrc);
    if (FAILED(hrc))
        RTPrintf(List::tr("    Suppressed opt.s: %Rhrc\n"), hrc);
    else if (Options.size() == 0)
        RTPrintf(List::tr("    Suppressed opts.: None\n"));
    else
    {
        RTPrintf(List::tr("    Suppressed opts.: "));
        for (size_t i = 0; i < Options.size(); i++)
            RTPrintf(i ? ", %u" : "%u", Options[i]);
        RTPrintf("\n");
    }

    com::SafeArray<DHCPOptionEncoding_T> Encodings;
    com::SafeArray<BSTR>                 Values;
    CHECK_ERROR2_STMT(hrc, ptrConfig, GetAllOptions(ComSafeArrayAsOutParam(Options),
                                                    ComSafeArrayAsOutParam(Encodings),
                                                    ComSafeArrayAsOutParam(Values)), hrcRet = hrc);
    if (FAILED(hrc))
        RTPrintf(List::tr("    DHCP options:     %Rhrc\n"), hrc);
    else if (Options.size() != Encodings.size() || Options.size() != Values.size())
    {
        RTPrintf(List::tr("    DHCP options:     Return count mismatch: %zu, %zu, %zu\n"),
                 Options.size(), Encodings.size(), Values.size());
        hrcRet = E_FAIL;
    }
    else if (Options.size() == 0)
        RTPrintf(List::tr("    DHCP options:     None\n"));
    else
        for (size_t i = 0; i < Options.size(); i++)
        {
            switch (Encodings[i])
            {
                case DHCPOptionEncoding_Normal:
                    RTPrintf(List::tr("      %3d/legacy: %ls\n"), Options[i], Values[i]);
                    break;
                case DHCPOptionEncoding_Hex:
                    RTPrintf("      %3d/hex:    %ls\n", Options[i], Values[i]);
                    break;
                default:
                    RTPrintf("      %3d/%u?: %ls\n", Options[i], Encodings[i], Values[i]);
                    break;
            }
        }

    return S_OK;
}


/**
 * List DHCP servers.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listDhcpServers(const ComPtr<IVirtualBox> &pVirtualBox)
{
    HRESULT hrcRet = S_OK;
    com::SafeIfaceArray<IDHCPServer> DHCPServers;
    CHECK_ERROR2I_RET(pVirtualBox, COMGETTER(DHCPServers)(ComSafeArrayAsOutParam(DHCPServers)), hrcCheck);
    for (size_t i = 0; i < DHCPServers.size(); ++i)
    {
        if (i > 0)
            RTPrintf("\n");

        ComPtr<IDHCPServer> ptrDHCPServer = DHCPServers[i];
        Bstr bstr;
        CHECK_ERROR2I_STMT(ptrDHCPServer, COMGETTER(NetworkName)(bstr.asOutParam()), hrcRet = hrcCheck);
        RTPrintf(List::tr("NetworkName:    %ls\n"), bstr.raw());

        CHECK_ERROR2I_STMT(ptrDHCPServer, COMGETTER(IPAddress)(bstr.asOutParam()), hrcRet = hrcCheck);
        RTPrintf("Dhcpd IP:       %ls\n", bstr.raw());

        CHECK_ERROR2I_STMT(ptrDHCPServer, COMGETTER(LowerIP)(bstr.asOutParam()), hrcRet = hrcCheck);
        RTPrintf(List::tr("LowerIPAddress: %ls\n"), bstr.raw());

        CHECK_ERROR2I_STMT(ptrDHCPServer, COMGETTER(UpperIP)(bstr.asOutParam()), hrcRet = hrcCheck);
        RTPrintf(List::tr("UpperIPAddress: %ls\n"), bstr.raw());

        CHECK_ERROR2I_STMT(ptrDHCPServer, COMGETTER(NetworkMask)(bstr.asOutParam()), hrcRet = hrcCheck);
        RTPrintf(List::tr("NetworkMask:    %ls\n"), bstr.raw());

        BOOL fEnabled = FALSE;
        CHECK_ERROR2I_STMT(ptrDHCPServer, COMGETTER(Enabled)(&fEnabled), hrcRet = hrcCheck);
        RTPrintf(List::tr("Enabled:        %s\n"), fEnabled ? List::tr("Yes") : List::tr("No"));

        /* Global configuration: */
        RTPrintf(List::tr("Global Configuration:\n"));
        HRESULT hrc;
        ComPtr<IDHCPGlobalConfig> ptrGlobal;
        CHECK_ERROR2_STMT(hrc, ptrDHCPServer, COMGETTER(GlobalConfig)(ptrGlobal.asOutParam()), hrcRet = hrc);
        if (SUCCEEDED(hrc))
        {
            hrc = showDhcpConfig(ptrGlobal);
            if (FAILED(hrc))
                hrcRet = hrc;
        }

        /* Group configurations: */
        com::SafeIfaceArray<IDHCPGroupConfig> Groups;
        CHECK_ERROR2_STMT(hrc, ptrDHCPServer, COMGETTER(GroupConfigs)(ComSafeArrayAsOutParam(Groups)), hrcRet = hrc);
        if (FAILED(hrc))
            RTPrintf(List::tr("Groups:               %Rrc\n"), hrc);
        else if (Groups.size() == 0)
            RTPrintf(List::tr("Groups:               None\n"));
        else
        {
            for (size_t iGrp = 0; iGrp < Groups.size(); iGrp++)
            {
                CHECK_ERROR2I_STMT(Groups[iGrp], COMGETTER(Name)(bstr.asOutParam()), hrcRet = hrcCheck);
                RTPrintf(List::tr("Group:                %ls\n"), bstr.raw());

                com::SafeIfaceArray<IDHCPGroupCondition> Conditions;
                CHECK_ERROR2_STMT(hrc, Groups[iGrp], COMGETTER(Conditions)(ComSafeArrayAsOutParam(Conditions)), hrcRet = hrc);
                if (FAILED(hrc))
                    RTPrintf(List::tr("    Conditions:       %Rhrc\n"), hrc);
                else if (Conditions.size() == 0)
                    RTPrintf(List::tr("    Conditions:       None\n"));
                else
                    for (size_t iCond = 0; iCond < Conditions.size(); iCond++)
                    {
                        BOOL fInclusive = TRUE;
                        CHECK_ERROR2_STMT(hrc, Conditions[iCond], COMGETTER(Inclusive)(&fInclusive), hrcRet = hrc);
                        DHCPGroupConditionType_T enmType = DHCPGroupConditionType_MAC;
                        CHECK_ERROR2_STMT(hrc, Conditions[iCond], COMGETTER(Type)(&enmType), hrcRet = hrc);
                        CHECK_ERROR2_STMT(hrc, Conditions[iCond], COMGETTER(Value)(bstr.asOutParam()), hrcRet = hrc);

                        RTPrintf(List::tr("    Conditions:       %s %s %ls\n"),
                                 fInclusive ? List::tr("include") : List::tr("exclude"),
                                   enmType == DHCPGroupConditionType_MAC                    ? "MAC       "
                                 : enmType == DHCPGroupConditionType_MACWildcard            ? "MAC*      "
                                 : enmType == DHCPGroupConditionType_vendorClassID          ? "VendorCID "
                                 : enmType == DHCPGroupConditionType_vendorClassIDWildcard  ? "VendorCID*"
                                 : enmType == DHCPGroupConditionType_userClassID            ? "UserCID   "
                                 : enmType == DHCPGroupConditionType_userClassIDWildcard    ? "UserCID*  "
                                 :                                                            "!UNKNOWN! ",
                                 bstr.raw());
                    }

                hrc = showDhcpConfig(Groups[iGrp]);
                if (FAILED(hrc))
                    hrcRet = hrc;
            }
            Groups.setNull();
        }

        /* Individual host / NIC configurations: */
        com::SafeIfaceArray<IDHCPIndividualConfig> Hosts;
        CHECK_ERROR2_STMT(hrc, ptrDHCPServer, COMGETTER(IndividualConfigs)(ComSafeArrayAsOutParam(Hosts)), hrcRet = hrc);
        if (FAILED(hrc))
            RTPrintf(List::tr("Individual Configs:   %Rrc\n"), hrc);
        else if (Hosts.size() == 0)
            RTPrintf(List::tr("Individual Configs:   None\n"));
        else
        {
            for (size_t iHost = 0; iHost < Hosts.size(); iHost++)
            {
                DHCPConfigScope_T enmScope = DHCPConfigScope_MAC;
                CHECK_ERROR2I_STMT(Hosts[iHost], COMGETTER(Scope)(&enmScope), hrcRet = hrcCheck);

                if (enmScope == DHCPConfigScope_MAC)
                {
                    CHECK_ERROR2I_STMT(Hosts[iHost], COMGETTER(MACAddress)(bstr.asOutParam()), hrcRet = hrcCheck);
                    RTPrintf(List::tr("Individual Config:    MAC %ls\n"), bstr.raw());
                }
                else
                {
                    ULONG uSlot = 0;
                    CHECK_ERROR2I_STMT(Hosts[iHost], COMGETTER(Slot)(&uSlot), hrcRet = hrcCheck);
                    CHECK_ERROR2I_STMT(Hosts[iHost], COMGETTER(MachineId)(bstr.asOutParam()), hrcRet = hrcCheck);
                    Bstr bstrMACAddress;
                    hrc = Hosts[iHost]->COMGETTER(MACAddress)(bstrMACAddress.asOutParam()); /* No CHECK_ERROR2 stuff! */
                    if (SUCCEEDED(hrc))
                        RTPrintf(List::tr("Individual Config:    VM NIC: %ls slot %u, MAC %ls\n"), bstr.raw(), uSlot,
                                 bstrMACAddress.raw());
                    else
                        RTPrintf(List::tr("Individual Config:    VM NIC: %ls slot %u, MAC %Rhrc\n"), bstr.raw(), uSlot, hrc);
                }

                CHECK_ERROR2I_STMT(Hosts[iHost], COMGETTER(FixedAddress)(bstr.asOutParam()), hrcRet = hrcCheck);
                if (bstr.isNotEmpty())
                    RTPrintf(List::tr("    Fixed Address:    %ls\n"), bstr.raw());
                else
                    RTPrintf(List::tr("    Fixed Address:    dynamic\n"));

                hrc = showDhcpConfig(Hosts[iHost]);
                if (FAILED(hrc))
                    hrcRet = hrc;
            }
            Hosts.setNull();
        }
    }

    return hrcRet;
}

/**
 * List extension packs.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listExtensionPacks(const ComPtr<IVirtualBox> &pVirtualBox)
{
    ComObjPtr<IExtPackManager> ptrExtPackMgr;
    CHECK_ERROR2I_RET(pVirtualBox, COMGETTER(ExtensionPackManager)(ptrExtPackMgr.asOutParam()), hrcCheck);

    SafeIfaceArray<IExtPack> extPacks;
    CHECK_ERROR2I_RET(ptrExtPackMgr, COMGETTER(InstalledExtPacks)(ComSafeArrayAsOutParam(extPacks)), hrcCheck);
    RTPrintf(List::tr("Extension Packs: %u\n"), extPacks.size());

    HRESULT hrc = S_OK;
    for (size_t i = 0; i < extPacks.size(); i++)
    {
        /* Read all the properties. */
        Bstr bstrName;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(Name)(bstrName.asOutParam()),          hrc = hrcCheck; bstrName.setNull());
        Bstr bstrDesc;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(Description)(bstrDesc.asOutParam()),   hrc = hrcCheck; bstrDesc.setNull());
        Bstr bstrVersion;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(Version)(bstrVersion.asOutParam()),    hrc = hrcCheck; bstrVersion.setNull());
        ULONG uRevision;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(Revision)(&uRevision),                 hrc = hrcCheck; uRevision = 0);
        Bstr bstrEdition;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(Edition)(bstrEdition.asOutParam()),    hrc = hrcCheck; bstrEdition.setNull());
        Bstr bstrVrdeModule;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(VRDEModule)(bstrVrdeModule.asOutParam()),hrc=hrcCheck; bstrVrdeModule.setNull());
        Bstr bstrCryptoModule;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(CryptoModule)(bstrCryptoModule.asOutParam()),hrc=hrcCheck; bstrCryptoModule.setNull());
        BOOL fUsable;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(Usable)(&fUsable),                     hrc = hrcCheck; fUsable = FALSE);
        Bstr bstrWhy;
        CHECK_ERROR2I_STMT(extPacks[i], COMGETTER(WhyUnusable)(bstrWhy.asOutParam()),    hrc = hrcCheck; bstrWhy.setNull());

        /* Display them. */
        if (i)
            RTPrintf("\n");
        RTPrintf(List::tr(
                   "Pack no.%2zu:   %ls\n"
                   "Version:        %ls\n"
                   "Revision:       %u\n"
                   "Edition:        %ls\n"
                   "Description:    %ls\n"
                   "VRDE Module:    %ls\n"
                   "Crypto Module:  %ls\n"
                   "Usable:         %RTbool\n"
                   "Why unusable:   %ls\n"),
                 i, bstrName.raw(),
                 bstrVersion.raw(),
                 uRevision,
                 bstrEdition.raw(),
                 bstrDesc.raw(),
                 bstrVrdeModule.raw(),
                 bstrCryptoModule.raw(),
                 fUsable != FALSE,
                 bstrWhy.raw());

        /* Query plugins and display them. */
    }
    return hrc;
}


/**
 * List machine groups.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT listGroups(const ComPtr<IVirtualBox> &pVirtualBox)
{
    SafeArray<BSTR> groups;
    CHECK_ERROR2I_RET(pVirtualBox, COMGETTER(MachineGroups)(ComSafeArrayAsOutParam(groups)), hrcCheck);

    for (size_t i = 0; i < groups.size(); i++)
    {
        RTPrintf("\"%ls\"\n", groups[i]);
    }
    return S_OK;
}


/**
 * List video capture devices.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox pointer.
 */
static HRESULT listVideoInputDevices(const ComPtr<IVirtualBox> &pVirtualBox)
{
    HRESULT hrc;
    ComPtr<IHost> host;
    CHECK_ERROR(pVirtualBox, COMGETTER(Host)(host.asOutParam()));
    com::SafeIfaceArray<IHostVideoInputDevice> hostVideoInputDevices;
    CHECK_ERROR(host, COMGETTER(VideoInputDevices)(ComSafeArrayAsOutParam(hostVideoInputDevices)));
    RTPrintf(List::tr("Video Input Devices: %u\n"), hostVideoInputDevices.size());
    for (size_t i = 0; i < hostVideoInputDevices.size(); ++i)
    {
        ComPtr<IHostVideoInputDevice> p = hostVideoInputDevices[i];
        Bstr name;
        p->COMGETTER(Name)(name.asOutParam());
        Bstr path;
        p->COMGETTER(Path)(path.asOutParam());
        Bstr alias;
        p->COMGETTER(Alias)(alias.asOutParam());
        RTPrintf("%ls \"%ls\"\n%ls\n", alias.raw(), name.raw(), path.raw());
    }
    return hrc;
}

/**
 * List supported screen shot formats.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox pointer.
 */
static HRESULT listScreenShotFormats(const ComPtr<IVirtualBox> &pVirtualBox)
{
    HRESULT hrc = S_OK;
    ComPtr<ISystemProperties> systemProperties;
    CHECK_ERROR(pVirtualBox, COMGETTER(SystemProperties)(systemProperties.asOutParam()));
    com::SafeArray<BitmapFormat_T> formats;
    CHECK_ERROR(systemProperties, COMGETTER(ScreenShotFormats)(ComSafeArrayAsOutParam(formats)));

    RTPrintf(List::tr("Supported %d screen shot formats:\n", "", formats.size()), formats.size());
    for (size_t i = 0; i < formats.size(); ++i)
    {
        uint32_t u32Format = (uint32_t)formats[i];
        char szFormat[5];
        szFormat[0] = RT_BYTE1(u32Format);
        szFormat[1] = RT_BYTE2(u32Format);
        szFormat[2] = RT_BYTE3(u32Format);
        szFormat[3] = RT_BYTE4(u32Format);
        szFormat[4] = 0;
        RTPrintf("    BitmapFormat_%s (0x%08X)\n", szFormat, u32Format);
    }
    return hrc;
}

/**
 * List available cloud providers.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox pointer.
 */
static HRESULT listCloudProviders(const ComPtr<IVirtualBox> &pVirtualBox)
{
    HRESULT hrc = S_OK;
    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR(pVirtualBox, COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()));
    com::SafeIfaceArray<ICloudProvider> apCloudProviders;
    CHECK_ERROR(pCloudProviderManager, COMGETTER(Providers)(ComSafeArrayAsOutParam(apCloudProviders)));

    RTPrintf(List::tr("Supported %d cloud providers:\n", "", apCloudProviders.size()), apCloudProviders.size());
    for (size_t i = 0; i < apCloudProviders.size(); ++i)
    {
        ComPtr<ICloudProvider> pCloudProvider = apCloudProviders[i];
        Bstr bstrProviderName;
        pCloudProvider->COMGETTER(Name)(bstrProviderName.asOutParam());
        RTPrintf(List::tr("Name:            %ls\n"), bstrProviderName.raw());
        pCloudProvider->COMGETTER(ShortName)(bstrProviderName.asOutParam());
        RTPrintf(List::tr("Short Name:      %ls\n"), bstrProviderName.raw());
        Bstr bstrProviderID;
        pCloudProvider->COMGETTER(Id)(bstrProviderID.asOutParam());
        RTPrintf("GUID:            %ls\n", bstrProviderID.raw());

        RTPrintf("\n");
    }
    return hrc;
}


/**
 * List all available cloud profiles (by iterating over the cloud providers).
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox pointer.
 * @param   fOptLong            If true, list all profile properties.
 */
static HRESULT listCloudProfiles(const ComPtr<IVirtualBox> &pVirtualBox, bool fOptLong)
{
    HRESULT hrc = S_OK;
    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR(pVirtualBox, COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()));
    com::SafeIfaceArray<ICloudProvider> apCloudProviders;
    CHECK_ERROR(pCloudProviderManager, COMGETTER(Providers)(ComSafeArrayAsOutParam(apCloudProviders)));

    for (size_t i = 0; i < apCloudProviders.size(); ++i)
    {
        ComPtr<ICloudProvider> pCloudProvider = apCloudProviders[i];
        com::SafeIfaceArray<ICloudProfile> apCloudProfiles;
        CHECK_ERROR(pCloudProvider, COMGETTER(Profiles)(ComSafeArrayAsOutParam(apCloudProfiles)));
        for (size_t j = 0; j < apCloudProfiles.size(); ++j)
        {
            ComPtr<ICloudProfile> pCloudProfile = apCloudProfiles[j];
            Bstr bstrProfileName;
            pCloudProfile->COMGETTER(Name)(bstrProfileName.asOutParam());
            RTPrintf(List::tr("Name:          %ls\n"), bstrProfileName.raw());
            Bstr bstrProviderID;
            pCloudProfile->COMGETTER(ProviderId)(bstrProviderID.asOutParam());
            RTPrintf(List::tr("Provider GUID: %ls\n"), bstrProviderID.raw());

            if (fOptLong)
            {
                com::SafeArray<BSTR> names;
                com::SafeArray<BSTR> values;
                pCloudProfile->GetProperties(Bstr().raw(), ComSafeArrayAsOutParam(names), ComSafeArrayAsOutParam(values));
                size_t cNames = names.size();
                size_t cValues = values.size();
                bool fFirst = true;
                for (size_t k = 0; k < cNames; k++)
                {
                    Bstr value;
                    if (k < cValues)
                        value = values[k];
                    RTPrintf("%s%ls=%ls\n",
                             fFirst ? List::tr("Property:      ") : "               ",
                             names[k], value.raw());
                    fFirst = false;
                }
            }

            RTPrintf("\n");
        }
    }
    return hrc;
}

static HRESULT displayCPUProfile(ICPUProfile *pProfile, size_t idx, int cchIdx, bool fOptLong, HRESULT hrc)
{
    /* Retrieve the attributes needed for both long and short display. */
    Bstr bstrName;
    CHECK_ERROR2I_RET(pProfile, COMGETTER(Name)(bstrName.asOutParam()), hrcCheck);

    CPUArchitecture_T enmArchitecture = CPUArchitecture_Any;
    CHECK_ERROR2I_RET(pProfile, COMGETTER(Architecture)(&enmArchitecture), hrcCheck);
    const char *pszArchitecture = "???";
    switch (enmArchitecture)
    {
        case CPUArchitecture_x86:      pszArchitecture = "x86"; break;
        case CPUArchitecture_AMD64:    pszArchitecture = "AMD64"; break;
        case CPUArchitecture_ARMv8_32: pszArchitecture = "ARMv8 (32-bit only)"; break;
        case CPUArchitecture_ARMv8_64: pszArchitecture = "ARMv8 (64-bit)"; break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case CPUArchitecture_32BitHack:
#endif
        case CPUArchitecture_Any:
            break;
    }

    /* Print what we've got. */
    if (!fOptLong)
        RTPrintf("#%0*zu: %ls [%s]\n", cchIdx, idx, bstrName.raw(), pszArchitecture);
    else
    {
        RTPrintf(List::tr("CPU Profile #%02zu:\n"), idx);
        RTPrintf(List::tr("  Architecture: %s\n"), pszArchitecture);
        RTPrintf(List::tr("  Name:         %ls\n"), bstrName.raw());
        CHECK_ERROR2I_RET(pProfile, COMGETTER(FullName)(bstrName.asOutParam()), hrcCheck);
        RTPrintf(List::tr("  Full Name:    %ls\n"), bstrName.raw());
    }
    return hrc;
}


/**
 * List all CPU profiles.
 *
 * @returns See produceList.
 * @param   ptrVirtualBox       Reference to the smart IVirtualBox pointer.
 * @param   fOptLong            If true, list all profile properties.
 * @param   fOptSorted          Sort the output if true, otherwise display in
 *                              system order.
 */
static HRESULT listCPUProfiles(const ComPtr<IVirtualBox> &ptrVirtualBox, bool fOptLong, bool fOptSorted)
{
    ComPtr<ISystemProperties> ptrSysProps;
    CHECK_ERROR2I_RET(ptrVirtualBox, COMGETTER(SystemProperties)(ptrSysProps.asOutParam()), hrcCheck);
    com::SafeIfaceArray<ICPUProfile> aCPUProfiles;
    CHECK_ERROR2I_RET(ptrSysProps, GetCPUProfiles(CPUArchitecture_Any, Bstr().raw(),
                                                  ComSafeArrayAsOutParam(aCPUProfiles)), hrcCheck);

    int const cchIdx = 1 + (aCPUProfiles.size() >= 10) + (aCPUProfiles.size() >= 100);

    HRESULT hrc = S_OK;
    if (!fOptSorted)
        for (size_t i = 0; i < aCPUProfiles.size(); i++)
            hrc = displayCPUProfile(aCPUProfiles[i], i, cchIdx, fOptLong, hrc);
    else
    {
        std::vector<std::pair<com::Bstr, ICPUProfile *> > vecSortedProfiles;
        for (size_t i = 0; i < aCPUProfiles.size(); ++i)
        {
            Bstr bstrName;
            CHECK_ERROR2I_RET(aCPUProfiles[i], COMGETTER(Name)(bstrName.asOutParam()), hrcCheck);
            try
            {
                vecSortedProfiles.push_back(std::pair<com::Bstr, ICPUProfile *>(bstrName, aCPUProfiles[i]));
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }
        }

        std::sort(vecSortedProfiles.begin(), vecSortedProfiles.end());

        for (size_t i = 0; i < vecSortedProfiles.size(); i++)
            hrc = displayCPUProfile(vecSortedProfiles[i].second, i, cchIdx, fOptLong, hrc);
    }

    return hrc;
}


/**
 * Translates PartitionType_T to a string if possible.
 * @returns read-only string if known value, @a pszUnknown if not.
 */
static const char *PartitionTypeToString(PartitionType_T enmType, const char *pszUnknown)
{
#define MY_CASE_STR(a_Type) case RT_CONCAT(PartitionType_,a_Type): return #a_Type
    switch (enmType)
    {
        MY_CASE_STR(Empty);
        MY_CASE_STR(FAT12);
        MY_CASE_STR(FAT16);
        MY_CASE_STR(FAT);
        MY_CASE_STR(IFS);
        MY_CASE_STR(FAT32CHS);
        MY_CASE_STR(FAT32LBA);
        MY_CASE_STR(FAT16B);
        MY_CASE_STR(Extended);
        MY_CASE_STR(WindowsRE);
        MY_CASE_STR(LinuxSwapOld);
        MY_CASE_STR(LinuxOld);
        MY_CASE_STR(DragonFlyBSDSlice);
        MY_CASE_STR(LinuxSwap);
        MY_CASE_STR(Linux);
        MY_CASE_STR(LinuxExtended);
        MY_CASE_STR(LinuxLVM);
        MY_CASE_STR(BSDSlice);
        MY_CASE_STR(AppleUFS);
        MY_CASE_STR(AppleHFS);
        MY_CASE_STR(Solaris);
        MY_CASE_STR(GPT);
        MY_CASE_STR(EFI);
        MY_CASE_STR(Unknown);
        MY_CASE_STR(MBR);
        MY_CASE_STR(iFFS);
        MY_CASE_STR(SonyBoot);
        MY_CASE_STR(LenovoBoot);
        MY_CASE_STR(WindowsMSR);
        MY_CASE_STR(WindowsBasicData);
        MY_CASE_STR(WindowsLDMMeta);
        MY_CASE_STR(WindowsLDMData);
        MY_CASE_STR(WindowsRecovery);
        MY_CASE_STR(WindowsStorageSpaces);
        MY_CASE_STR(WindowsStorageReplica);
        MY_CASE_STR(IBMGPFS);
        MY_CASE_STR(LinuxData);
        MY_CASE_STR(LinuxRAID);
        MY_CASE_STR(LinuxRootX86);
        MY_CASE_STR(LinuxRootAMD64);
        MY_CASE_STR(LinuxRootARM32);
        MY_CASE_STR(LinuxRootARM64);
        MY_CASE_STR(LinuxHome);
        MY_CASE_STR(LinuxSrv);
        MY_CASE_STR(LinuxPlainDmCrypt);
        MY_CASE_STR(LinuxLUKS);
        MY_CASE_STR(LinuxReserved);
        MY_CASE_STR(FreeBSDBoot);
        MY_CASE_STR(FreeBSDData);
        MY_CASE_STR(FreeBSDSwap);
        MY_CASE_STR(FreeBSDUFS);
        MY_CASE_STR(FreeBSDVinum);
        MY_CASE_STR(FreeBSDZFS);
        MY_CASE_STR(FreeBSDUnknown);
        MY_CASE_STR(AppleHFSPlus);
        MY_CASE_STR(AppleAPFS);
        MY_CASE_STR(AppleRAID);
        MY_CASE_STR(AppleRAIDOffline);
        MY_CASE_STR(AppleBoot);
        MY_CASE_STR(AppleLabel);
        MY_CASE_STR(AppleTvRecovery);
        MY_CASE_STR(AppleCoreStorage);
        MY_CASE_STR(SoftRAIDStatus);
        MY_CASE_STR(SoftRAIDScratch);
        MY_CASE_STR(SoftRAIDVolume);
        MY_CASE_STR(SoftRAIDCache);
        MY_CASE_STR(AppleUnknown);
        MY_CASE_STR(SolarisBoot);
        MY_CASE_STR(SolarisRoot);
        MY_CASE_STR(SolarisSwap);
        MY_CASE_STR(SolarisBackup);
        MY_CASE_STR(SolarisUsr);
        MY_CASE_STR(SolarisVar);
        MY_CASE_STR(SolarisHome);
        MY_CASE_STR(SolarisAltSector);
        MY_CASE_STR(SolarisReserved);
        MY_CASE_STR(SolarisUnknown);
        MY_CASE_STR(NetBSDSwap);
        MY_CASE_STR(NetBSDFFS);
        MY_CASE_STR(NetBSDLFS);
        MY_CASE_STR(NetBSDRAID);
        MY_CASE_STR(NetBSDConcatenated);
        MY_CASE_STR(NetBSDEncrypted);
        MY_CASE_STR(NetBSDUnknown);
        MY_CASE_STR(ChromeOSKernel);
        MY_CASE_STR(ChromeOSRootFS);
        MY_CASE_STR(ChromeOSFuture);
        MY_CASE_STR(ContLnxUsr);
        MY_CASE_STR(ContLnxRoot);
        MY_CASE_STR(ContLnxReserved);
        MY_CASE_STR(ContLnxRootRAID);
        MY_CASE_STR(HaikuBFS);
        MY_CASE_STR(MidntBSDBoot);
        MY_CASE_STR(MidntBSDData);
        MY_CASE_STR(MidntBSDSwap);
        MY_CASE_STR(MidntBSDUFS);
        MY_CASE_STR(MidntBSDVium);
        MY_CASE_STR(MidntBSDZFS);
        MY_CASE_STR(MidntBSDUnknown);
        MY_CASE_STR(OpenBSDData);
        MY_CASE_STR(QNXPowerSafeFS);
        MY_CASE_STR(Plan9);
        MY_CASE_STR(VMWareVMKCore);
        MY_CASE_STR(VMWareVMFS);
        MY_CASE_STR(VMWareReserved);
        MY_CASE_STR(VMWareUnknown);
        MY_CASE_STR(AndroidX86Bootloader);
        MY_CASE_STR(AndroidX86Bootloader2);
        MY_CASE_STR(AndroidX86Boot);
        MY_CASE_STR(AndroidX86Recovery);
        MY_CASE_STR(AndroidX86Misc);
        MY_CASE_STR(AndroidX86Metadata);
        MY_CASE_STR(AndroidX86System);
        MY_CASE_STR(AndroidX86Cache);
        MY_CASE_STR(AndroidX86Data);
        MY_CASE_STR(AndroidX86Persistent);
        MY_CASE_STR(AndroidX86Vendor);
        MY_CASE_STR(AndroidX86Config);
        MY_CASE_STR(AndroidX86Factory);
        MY_CASE_STR(AndroidX86FactoryAlt);
        MY_CASE_STR(AndroidX86Fastboot);
        MY_CASE_STR(AndroidX86OEM);
        MY_CASE_STR(AndroidARMMeta);
        MY_CASE_STR(AndroidARMExt);
        MY_CASE_STR(ONIEBoot);
        MY_CASE_STR(ONIEConfig);
        MY_CASE_STR(PowerPCPrep);
        MY_CASE_STR(XDGShrBootConfig);
        MY_CASE_STR(CephBlock);
        MY_CASE_STR(CephBlockDB);
        MY_CASE_STR(CephBlockDBDmc);
        MY_CASE_STR(CephBlockDBDmcLUKS);
        MY_CASE_STR(CephBlockDmc);
        MY_CASE_STR(CephBlockDmcLUKS);
        MY_CASE_STR(CephBlockWALog);
        MY_CASE_STR(CephBlockWALogDmc);
        MY_CASE_STR(CephBlockWALogDmcLUKS);
        MY_CASE_STR(CephDisk);
        MY_CASE_STR(CephDiskDmc);
        MY_CASE_STR(CephJournal);
        MY_CASE_STR(CephJournalDmc);
        MY_CASE_STR(CephJournalDmcLUKS);
        MY_CASE_STR(CephLockbox);
        MY_CASE_STR(CephMultipathBlock1);
        MY_CASE_STR(CephMultipathBlock2);
        MY_CASE_STR(CephMultipathBlockDB);
        MY_CASE_STR(CephMultipathBLockWALog);
        MY_CASE_STR(CephMultipathJournal);
        MY_CASE_STR(CephMultipathOSD);
        MY_CASE_STR(CephOSD);
        MY_CASE_STR(CephOSDDmc);
        MY_CASE_STR(CephOSDDmcLUKS);
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case PartitionType_32BitHack: break;
#endif
        /* no default! */
    }
#undef MY_CASE_STR
    return pszUnknown;
}


/**
 * List all available host drives with their partitions.
 *
 * @returns See produceList.
 * @param   pVirtualBox         Reference to the IVirtualBox pointer.
 * @param   fOptLong            Long listing or human readable.
 */
static HRESULT listHostDrives(const ComPtr<IVirtualBox> pVirtualBox, bool fOptLong)
{
    HRESULT hrc = S_OK;
    ComPtr<IHost> pHost;
    CHECK_ERROR2I_RET(pVirtualBox, COMGETTER(Host)(pHost.asOutParam()), hrcCheck);
    com::SafeIfaceArray<IHostDrive> apHostDrives;
    CHECK_ERROR2I_RET(pHost, COMGETTER(HostDrives)(ComSafeArrayAsOutParam(apHostDrives)), hrcCheck);
    for (size_t i = 0; i < apHostDrives.size(); ++i)
    {
        ComPtr<IHostDrive> pHostDrive = apHostDrives[i];

        /* The drivePath and model attributes are accessible even when the object
           is in 'limited' mode. */
        com::Bstr bstrDrivePath;
        CHECK_ERROR(pHostDrive,COMGETTER(DrivePath)(bstrDrivePath.asOutParam()));
        if (SUCCEEDED(hrc))
            RTPrintf(List::tr("%sDrive:       %ls\n"), i > 0 ? "\n" : "", bstrDrivePath.raw());
        else
            RTPrintf(List::tr("%sDrive:       %Rhrc\n"), i > 0 ? "\n" : "", hrc);

        com::Bstr bstrModel;
        CHECK_ERROR(pHostDrive,COMGETTER(Model)(bstrModel.asOutParam()));
        if (FAILED(hrc))
            RTPrintf(List::tr("Model:       %Rhrc\n"), hrc);
        else if (bstrModel.isNotEmpty())
            RTPrintf(List::tr("Model:       \"%ls\"\n"), bstrModel.raw());
        else
            RTPrintf(List::tr("Model:       unknown/inaccessible\n"));

        /* The other attributes are not accessible in limited mode and will fail
           with E_ACCESSDENIED.  Typically means the user cannot read the drive. */
        com::Bstr bstrUuidDisk;
        hrc = pHostDrive->COMGETTER(Uuid)(bstrUuidDisk.asOutParam());
        if (SUCCEEDED(hrc) && !com::Guid(bstrUuidDisk).isZero())
            RTPrintf("UUID:        %ls\n", bstrUuidDisk.raw());
        else if (hrc == E_ACCESSDENIED)
        {
            RTPrintf(List::tr("Further disk and partitioning information is not available for drive \"%ls\". (E_ACCESSDENIED)\n"),
                     bstrDrivePath.raw());
            continue;
        }
        else if (FAILED(hrc))
        {
            RTPrintf("UUID:        %Rhrc\n", hrc);
            com::GlueHandleComErrorNoCtx(pHostDrive, hrc);
        }

        LONG64 cbSize = 0;
        hrc = pHostDrive->COMGETTER(Size)(&cbSize);
        if (SUCCEEDED(hrc) && fOptLong)
            RTPrintf(List::tr("Size:        %llu bytes (%Rhcb)\n", "", cbSize), cbSize, cbSize);
        else if (SUCCEEDED(hrc))
            RTPrintf(List::tr("Size:        %Rhcb\n"), cbSize);
        else
        {
            RTPrintf(List::tr("Size:        %Rhrc\n"), hrc);
            com::GlueHandleComErrorNoCtx(pHostDrive, hrc);
        }

        ULONG cbSectorSize = 0;
        hrc = pHostDrive->COMGETTER(SectorSize)(&cbSectorSize);
        if (SUCCEEDED(hrc))
            RTPrintf(List::tr("Sector Size: %u bytes\n", "", cbSectorSize), cbSectorSize);
        else
        {
            RTPrintf(List::tr("Sector Size: %Rhrc\n"), hrc);
            com::GlueHandleComErrorNoCtx(pHostDrive, hrc);
        }

        PartitioningType_T partitioningType = (PartitioningType_T)9999;
        hrc = pHostDrive->COMGETTER(PartitioningType)(&partitioningType);
        if (SUCCEEDED(hrc))
            RTPrintf(List::tr("Scheme:      %s\n"), partitioningType == PartitioningType_MBR ? "MBR" : "GPT");
        else
        {
            RTPrintf(List::tr("Scheme:      %Rhrc\n"), hrc);
            com::GlueHandleComErrorNoCtx(pHostDrive, hrc);
        }

        com::SafeIfaceArray<IHostDrivePartition> apHostDrivesPartitions;
        hrc = pHostDrive->COMGETTER(Partitions)(ComSafeArrayAsOutParam(apHostDrivesPartitions));
        if (FAILED(hrc))
        {
            RTPrintf(List::tr("Partitions:  %Rhrc\n"), hrc);
            com::GlueHandleComErrorNoCtx(pHostDrive, hrc);
        }
        else if (apHostDrivesPartitions.size() == 0)
            RTPrintf(List::tr("Partitions:  None (or not able to grok them).\n"));
        else if (partitioningType == PartitioningType_MBR)
        {
            if (fOptLong)
                RTPrintf(List::tr("Partitions:                              First         Last\n"
                                  "##  Type      Byte Size     Byte Offset  Cyl/Head/Sec  Cyl/Head/Sec Active\n"));
            else
                RTPrintf(List::tr("Partitions:                   First         Last\n"
                                  "##  Type  Size      Start     Cyl/Head/Sec  Cyl/Head/Sec Active\n"));
            for (size_t j = 0; j < apHostDrivesPartitions.size(); ++j)
            {
                ComPtr<IHostDrivePartition> pHostDrivePartition = apHostDrivesPartitions[j];

                ULONG idx = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Number)(&idx));
                ULONG uType = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(TypeMBR)(&uType));
                ULONG uStartCylinder = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(StartCylinder)(&uStartCylinder));
                ULONG uStartHead = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(StartHead)(&uStartHead));
                ULONG uStartSector = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(StartSector)(&uStartSector));
                ULONG uEndCylinder = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(EndCylinder)(&uEndCylinder));
                ULONG uEndHead = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(EndHead)(&uEndHead));
                ULONG uEndSector = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(EndSector)(&uEndSector));
                cbSize = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Size)(&cbSize));
                LONG64 offStart = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Start)(&offStart));
                BOOL fActive = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Active)(&fActive));
                PartitionType_T enmType = PartitionType_Unknown;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Type)(&enmType));

                /* Max size & offset  here is around 16TiB with 4KiB sectors. */
                if (fOptLong) /* cb/off: max 16TiB; idx: max 64. */
                    RTPrintf("%2u   %02x  %14llu  %14llu  %4u/%3u/%2u   %4u/%3u/%2u    %s   %s\n",
                             idx, uType, cbSize, offStart,
                             uStartCylinder, uStartHead, uStartSector, uEndCylinder, uEndHead, uEndSector,
                             fActive ? List::tr("yes") : List::tr("no"), PartitionTypeToString(enmType, ""));
                else
                    RTPrintf("%2u   %02x   %8Rhcb  %8Rhcb  %4u/%3u/%2u   %4u/%3u/%2u   %s   %s\n",
                             idx, uType, (uint64_t)cbSize, (uint64_t)offStart,
                             uStartCylinder, uStartHead, uStartSector, uEndCylinder, uEndHead, uEndSector,
                             fActive ? List::tr("yes") : List::tr("no"), PartitionTypeToString(enmType, ""));
            }
        }
        else /* GPT */
        {
            /* Determin the max partition type length to try reduce the table width: */
            size_t cchMaxType = 0;
            for (size_t j = 0; j < apHostDrivesPartitions.size(); ++j)
            {
                ComPtr<IHostDrivePartition> pHostDrivePartition = apHostDrivesPartitions[j];
                PartitionType_T enmType = PartitionType_Unknown;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Type)(&enmType));
                size_t const cchTypeNm = strlen(PartitionTypeToString(enmType, "e530bf6d-2754-4e9d-b260-60a5d0b80457"));
                cchMaxType = RT_MAX(cchTypeNm, cchMaxType);
            }
            cchMaxType = RT_MIN(cchMaxType, RTUUID_STR_LENGTH);

            if (fOptLong)
                RTPrintf(List::tr(
                           "Partitions:\n"
                           "## %-*s Uuid                                           Byte Size         Byte Offset Active Name\n"),
                         (int)cchMaxType, List::tr("Type"));
            else
                RTPrintf(List::tr(
                           "Partitions:\n"
                           "##  %-*s  Uuid                                   Size      Start   Active Name\n"),
                         (int)cchMaxType, List::tr("Type"));

            for (size_t j = 0; j < apHostDrivesPartitions.size(); ++j)
            {
                ComPtr<IHostDrivePartition> pHostDrivePartition = apHostDrivesPartitions[j];

                ULONG idx = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Number)(&idx));
                com::Bstr bstrUuidType;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(TypeUuid)(bstrUuidType.asOutParam()));
                com::Bstr bstrUuidPartition;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Uuid)(bstrUuidPartition.asOutParam()));
                cbSize = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Size)(&cbSize));
                LONG64 offStart = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Start)(&offStart));
                BOOL fActive = 0;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Active)(&fActive));
                com::Bstr bstrName;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Name)(bstrName.asOutParam()));

                PartitionType_T enmType = PartitionType_Unknown;
                CHECK_ERROR(pHostDrivePartition, COMGETTER(Type)(&enmType));

                Utf8Str strTypeConv;
                const char *pszTypeNm = PartitionTypeToString(enmType, NULL);
                if (!pszTypeNm)
                    pszTypeNm = (strTypeConv = bstrUuidType).c_str();
                else if (strlen(pszTypeNm) >= RTUUID_STR_LENGTH /* includes '\0' */)
                    pszTypeNm -= RTUUID_STR_LENGTH - 1 - strlen(pszTypeNm);

                if (fOptLong)
                    RTPrintf("%2u %-*s %36ls %19llu %19llu   %-3s  %ls\n", idx, cchMaxType, pszTypeNm,
                             bstrUuidPartition.raw(), cbSize, offStart, fActive ? List::tr("on") : List::tr("off"),
                             bstrName.raw());
                else
                    RTPrintf("%2u  %-*s  %36ls  %8Rhcb  %8Rhcb  %-3s   %ls\n", idx, cchMaxType, pszTypeNm,
                             bstrUuidPartition.raw(), cbSize, offStart, fActive ? List::tr("on") : List::tr("off"),
                             bstrName.raw());
            }
        }
    }
    return hrc;
}


static HRESULT listExecutionEnginesForCpuArchitecture(CPUArchitecture_T enmCpuArchitecture, const ComPtr<ISystemProperties> ptrSysProps, const ComPtr<IHost> pHost,
                                                      HRESULT hrc)
{
    const char *pszArchitecture = "???";
    switch (enmCpuArchitecture)
    {
        case CPUArchitecture_x86:      pszArchitecture = "x86"; break;
        case CPUArchitecture_AMD64:    pszArchitecture = "AMD64"; break;
        case CPUArchitecture_ARMv8_32: pszArchitecture = "ARMv8 (32-bit only)"; break;
        case CPUArchitecture_ARMv8_64: pszArchitecture = "ARMv8 (64-bit)"; break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case CPUArchitecture_32BitHack:
#endif
        case CPUArchitecture_Any:
            break;
    }

    /* Query all possible execution engines supported by the build. */
    com::SafeArray<VMExecutionEngine_T> aExecutionEngines;
    CHECK_ERROR2I_RET(ptrSysProps, GetExecutionEnginesForVmCpuArchitecture(enmCpuArchitecture,
                                                                           ComSafeArrayAsOutParam(aExecutionEngines)), hrcCheck);

    /* Don't dump anything if the architecture is not supported at all. */
    if (aExecutionEngines.size())
    {
        /* Now filter out the ones the host actually supports. */
        std::vector<VMExecutionEngine_T> vecExecEnginesSupported;
        for (size_t i = 0; i < aExecutionEngines.size(); i++)
        {
            BOOL fSupported = FALSE;
            CHECK_ERROR2I_RET(pHost, IsExecutionEngineSupported(enmCpuArchitecture, aExecutionEngines[i], &fSupported), hrcCheck);
            if (fSupported)
                vecExecEnginesSupported.push_back(aExecutionEngines[i]);
        }

        RTPrintf(List::tr("CPU Architecture: %s\n"), pszArchitecture);

        /* Print what we've got. */
        for (size_t i = 0; i < vecExecEnginesSupported.size(); i++)
        {
            const char *pszExecEngine = "???";
            switch (vecExecEnginesSupported[i])
            {
                case VMExecutionEngine_Default:     pszExecEngine = "Default";     break;
                case VMExecutionEngine_HwVirt:      pszExecEngine = "HwVirt";      break;
                case VMExecutionEngine_NativeApi:   pszExecEngine = "NativeApi";   break;
                case VMExecutionEngine_Interpreter: pszExecEngine = "Interpreter"; break;
                case VMExecutionEngine_Recompiler:  pszExecEngine = "Recompiler";  break;
                default: break;
            }

            RTPrintf("  #%02zu: %s\n", i, pszExecEngine);
        }
    }

    return hrc;
}


/**
 * List all execution engines supported by the host.
 *
 * @returns See produceList.
 * @param   ptrVirtualBox       Reference to the smart IVirtualBox pointer.
 */
static HRESULT listExecutionEngines(const ComPtr<IVirtualBox> &ptrVirtualBox)
{
    ComPtr<ISystemProperties> ptrSysProps;
    CHECK_ERROR2I_RET(ptrVirtualBox, COMGETTER(SystemProperties)(ptrSysProps.asOutParam()), hrcCheck);
    ComPtr<IHost> pHost;
    CHECK_ERROR2I_RET(ptrVirtualBox, COMGETTER(Host)(pHost.asOutParam()), hrcCheck);

    HRESULT hrc = listExecutionEnginesForCpuArchitecture(CPUArchitecture_x86,      ptrSysProps, pHost, S_OK);
            hrc = listExecutionEnginesForCpuArchitecture(CPUArchitecture_AMD64,    ptrSysProps, pHost, hrc);
            hrc = listExecutionEnginesForCpuArchitecture(CPUArchitecture_ARMv8_32, ptrSysProps, pHost, hrc);
            hrc = listExecutionEnginesForCpuArchitecture(CPUArchitecture_ARMv8_64, ptrSysProps, pHost, hrc);

    return hrc;
}


/**
 * The type of lists we can produce.
 */
enum ListType_T
{
    kListNotSpecified = 1000,
    kListVMs,
    kListRunningVMs,
    kListOsTypes,
    kListOsSubtypes,
    kListHostDvds,
    kListHostFloppies,
    kListInternalNetworks,
    kListBridgedInterfaces,
#if defined(VBOX_WITH_NETFLT)
    kListHostOnlyInterfaces,
#endif
#if defined(VBOX_WITH_VMNET)
    kListHostOnlyNetworks,
#endif
#if defined(VBOX_WITH_CLOUD_NET)
    kListCloudNetworks,
#endif
    kListHostCpuIDs,
    kListHostInfo,
    kListHddBackends,
    kListHdds,
    kListDvds,
    kListFloppies,
    kListUsbHost,
    kListUsbFilters,
    kListSystemProperties,
#if defined(VBOX_WITH_UPDATE_AGENT)
    kListUpdateAgents,
#endif
    kListDhcpServers,
    kListExtPacks,
    kListGroups,
    kListNatNetworks,
    kListVideoInputDevices,
    kListScreenShotFormats,
    kListCloudProviders,
    kListCloudProfiles,
    kListCPUProfiles,
    kListHostDrives,
    kListExecutionEngines
};


/**
 * Produces the specified listing.
 *
 * @returns S_OK or some COM error code that has been reported in full.
 * @param   enmList             The list to produce.
 * @param   fOptLong            Long (@c true) or short list format.
 * @param   fOptSorted          Whether the output shall be sorted or not (depends on the actual command).
 * @param   enmPlatformArch     Filters the list by the given platform architecture,
 *                              or processes all platforms if PlatformArchitecture_None is specified.
 * @param   pVirtualBox         Reference to the IVirtualBox smart pointer.
 */
static HRESULT produceList(enum ListType_T enmCommand, bool fOptLong, bool fOptSorted, PlatformArchitecture_T enmPlatformArch,
                           const ComPtr<IVirtualBox> &pVirtualBox)
{
    HRESULT hrc = S_OK;
    switch (enmCommand)
    {
        case kListNotSpecified:
            AssertFailed();
            return E_FAIL;

        case kListVMs:
        {
            /*
             * Get the list of all registered VMs
             */
            com::SafeIfaceArray<IMachine> machines;
            hrc = pVirtualBox->COMGETTER(Machines)(ComSafeArrayAsOutParam(machines));
            if (SUCCEEDED(hrc))
            {
                /*
                 * Display it.
                 */
                if (!fOptSorted)
                {
                    for (size_t i = 0; i < machines.size(); ++i)
                        if (machines[i])
                            hrc = showVMInfo(pVirtualBox, machines[i], NULL, fOptLong ? VMINFO_STANDARD : VMINFO_COMPACT);
                }
                else
                {
                    /*
                     * Sort the list by name before displaying it.
                     */
                    std::vector<std::pair<com::Bstr, IMachine *> > sortedMachines;
                    for (size_t i = 0; i < machines.size(); ++i)
                    {
                        IMachine *pMachine = machines[i];
                        if (pMachine) /* no idea why we need to do this... */
                        {
                            Bstr bstrName;
                            pMachine->COMGETTER(Name)(bstrName.asOutParam());
                            sortedMachines.push_back(std::pair<com::Bstr, IMachine *>(bstrName, pMachine));
                        }
                    }

                    std::sort(sortedMachines.begin(), sortedMachines.end());

                    for (size_t i = 0; i < sortedMachines.size(); ++i)
                        hrc = showVMInfo(pVirtualBox, sortedMachines[i].second, NULL, fOptLong ? VMINFO_STANDARD : VMINFO_COMPACT);
                }
            }
            break;
        }

        case kListRunningVMs:
        {
            /*
             * Get the list of all _running_ VMs
             */
            com::SafeIfaceArray<IMachine> machines;
            hrc = pVirtualBox->COMGETTER(Machines)(ComSafeArrayAsOutParam(machines));
            com::SafeArray<MachineState_T> states;
            if (SUCCEEDED(hrc))
                hrc = pVirtualBox->GetMachineStates(ComSafeArrayAsInParam(machines), ComSafeArrayAsOutParam(states));
            if (SUCCEEDED(hrc))
            {
                /*
                 * Iterate through the collection
                 */
                for (size_t i = 0; i < machines.size(); ++i)
                {
                    if (machines[i])
                    {
                        MachineState_T machineState = states[i];
                        switch (machineState)
                        {
                            case MachineState_Running:
                            case MachineState_Teleporting:
                            case MachineState_LiveSnapshotting:
                            case MachineState_Paused:
                            case MachineState_TeleportingPausedVM:
                                hrc = showVMInfo(pVirtualBox, machines[i], NULL, fOptLong ? VMINFO_STANDARD : VMINFO_COMPACT);
                                break;
                            default: break; /* Shut up MSC */
                        }
                    }
                }
            }
            break;
        }

        case kListOsTypes:
        {
            com::SafeIfaceArray<IGuestOSType> coll;
            hrc = pVirtualBox->COMGETTER(GuestOSTypes)(ComSafeArrayAsOutParam(coll));
            if (SUCCEEDED(hrc))
                listGuestOSTypes(coll, fOptLong, fOptSorted, enmPlatformArch);
            break;
        }

        case kListOsSubtypes:
        {
            com::SafeArray<BSTR> GuestOSFamilies;
            CHECK_ERROR(pVirtualBox, COMGETTER(GuestOSFamilies)(ComSafeArrayAsOutParam(GuestOSFamilies)));
            if (SUCCEEDED(hrc))
            {
                for (size_t i = 0; i < GuestOSFamilies.size(); ++i)
                {
                    const Bstr bstrOSFamily = GuestOSFamilies[i];
                    com::SafeArray<BSTR> GuestOSSubtypes;
                    CHECK_ERROR(pVirtualBox,
                                GetGuestOSSubtypesByFamilyId(bstrOSFamily.raw(),
                                                             ComSafeArrayAsOutParam(GuestOSSubtypes)));
                    if (SUCCEEDED(hrc))
                    {
                        RTPrintf("%ls\n", bstrOSFamily.raw());
                        for (size_t j = 0; j < GuestOSSubtypes.size(); ++j)
                        {
                            RTPrintf("\t%ls\n", GuestOSSubtypes[j]);
                            com::SafeArray<BSTR> GuestOSDescs;
                            const Bstr bstrOSSubtype = GuestOSSubtypes[j];
                            CHECK_ERROR(pVirtualBox,
                                        GetGuestOSDescsBySubtype(bstrOSSubtype.raw(),
                                                                 ComSafeArrayAsOutParam(GuestOSDescs)));
                            if (SUCCEEDED(hrc))
                                for (size_t k = 0; k < GuestOSDescs.size(); ++k)
                                    RTPrintf("\t\t%ls\n", GuestOSDescs[k]);
                        }
                    }
                }
            }
            break;
        }

        case kListHostDvds:
        {
            ComPtr<IHost> host;
            CHECK_ERROR(pVirtualBox, COMGETTER(Host)(host.asOutParam()));
            com::SafeIfaceArray<IMedium> coll;
            CHECK_ERROR(host, COMGETTER(DVDDrives)(ComSafeArrayAsOutParam(coll)));
            if (SUCCEEDED(hrc))
            {
                for (size_t i = 0; i < coll.size(); ++i)
                {
                    ComPtr<IMedium> dvdDrive = coll[i];
                    Bstr uuid;
                    dvdDrive->COMGETTER(Id)(uuid.asOutParam());
                    RTPrintf("UUID:         %s\n", Utf8Str(uuid).c_str());
                    Bstr location;
                    dvdDrive->COMGETTER(Location)(location.asOutParam());
                    RTPrintf(List::tr("Name:         %ls\n\n"), location.raw());
                }
            }
            break;
        }

        case kListHostFloppies:
        {
            ComPtr<IHost> host;
            CHECK_ERROR(pVirtualBox, COMGETTER(Host)(host.asOutParam()));
            com::SafeIfaceArray<IMedium> coll;
            CHECK_ERROR(host, COMGETTER(FloppyDrives)(ComSafeArrayAsOutParam(coll)));
            if (SUCCEEDED(hrc))
            {
                for (size_t i = 0; i < coll.size(); ++i)
                {
                    ComPtr<IMedium> floppyDrive = coll[i];
                    Bstr uuid;
                    floppyDrive->COMGETTER(Id)(uuid.asOutParam());
                    RTPrintf("UUID:         %s\n", Utf8Str(uuid).c_str());
                    Bstr location;
                    floppyDrive->COMGETTER(Location)(location.asOutParam());
                    RTPrintf(List::tr("Name:         %ls\n\n"), location.raw());
                }
            }
            break;
        }

        case kListInternalNetworks:
            hrc = listInternalNetworks(pVirtualBox);
            break;

        case kListBridgedInterfaces:
#if defined(VBOX_WITH_NETFLT)
        case kListHostOnlyInterfaces:
#endif
            hrc = listNetworkInterfaces(pVirtualBox, enmCommand == kListBridgedInterfaces);
            break;

#if defined(VBOX_WITH_VMNET)
        case kListHostOnlyNetworks:
            hrc = listHostOnlyNetworks(pVirtualBox);
            break;
#endif

#if defined(VBOX_WITH_CLOUD_NET)
        case kListCloudNetworks:
            hrc = listCloudNetworks(pVirtualBox);
            break;
#endif
        case kListHostInfo:
            hrc = listHostInfo(pVirtualBox);
            break;

        case kListHostCpuIDs:
        {
            ComPtr<IHost> Host;
            CHECK_ERROR_BREAK(pVirtualBox, COMGETTER(Host)(Host.asOutParam()));
            PlatformArchitecture_T platformArch;
            CHECK_ERROR_BREAK(Host, COMGETTER(Architecture)(&platformArch));

            switch (platformArch)
            {
                case PlatformArchitecture_x86:
                {
                    ComPtr<IHostX86> HostX86;
                    CHECK_ERROR_BREAK(Host, COMGETTER(X86)(HostX86.asOutParam()));

                    RTPrintf(List::tr("Host CPUIDs:\n\nLeaf no.  EAX      EBX      ECX      EDX\n"));
                    ULONG uCpuNo = 0; /* ASSUMES that CPU#0 is online. */
                    static uint32_t const s_auCpuIdRanges[] =
                    {
                        UINT32_C(0x00000000), UINT32_C(0x0000007f),
                        UINT32_C(0x80000000), UINT32_C(0x8000007f),
                        UINT32_C(0xc0000000), UINT32_C(0xc000007f)
                    };
                    for (unsigned i = 0; i < RT_ELEMENTS(s_auCpuIdRanges); i += 2)
                    {
                        ULONG uEAX, uEBX, uECX, uEDX, cLeafs;
                        CHECK_ERROR(HostX86, GetProcessorCPUIDLeaf(uCpuNo, s_auCpuIdRanges[i], 0, &cLeafs, &uEBX, &uECX, &uEDX));
                        if (cLeafs < s_auCpuIdRanges[i] || cLeafs > s_auCpuIdRanges[i+1])
                            continue;
                        cLeafs++;
                        for (ULONG iLeaf = s_auCpuIdRanges[i]; iLeaf <= cLeafs; iLeaf++)
                        {
                            CHECK_ERROR(HostX86, GetProcessorCPUIDLeaf(uCpuNo, iLeaf, 0, &uEAX, &uEBX, &uECX, &uEDX));
                            RTPrintf("%08x  %08x %08x %08x %08x\n", iLeaf, uEAX, uEBX, uECX, uEDX);
                        }
                    }

                    break;
                }

                case PlatformArchitecture_ARM:
                {
                    /** @todo BUGBUG Implement this for ARM! */
                    break;
                }

                default:
                    AssertFailed();
                    break;
            }
            break;
        }

        case kListHddBackends:
            hrc = listHddBackends(pVirtualBox);
            break;

        case kListHdds:
        {
            com::SafeIfaceArray<IMedium> hdds;
            CHECK_ERROR(pVirtualBox, COMGETTER(HardDisks)(ComSafeArrayAsOutParam(hdds)));
            hrc = listMedia(pVirtualBox, hdds, List::tr("base"), fOptLong);
            break;
        }

        case kListDvds:
        {
            com::SafeIfaceArray<IMedium> dvds;
            CHECK_ERROR(pVirtualBox, COMGETTER(DVDImages)(ComSafeArrayAsOutParam(dvds)));
            hrc = listMedia(pVirtualBox, dvds, NULL, fOptLong);
            break;
        }

        case kListFloppies:
        {
            com::SafeIfaceArray<IMedium> floppies;
            CHECK_ERROR(pVirtualBox, COMGETTER(FloppyImages)(ComSafeArrayAsOutParam(floppies)));
            hrc = listMedia(pVirtualBox, floppies, NULL, fOptLong);
            break;
        }

        case kListUsbHost:
            hrc = listUsbHost(pVirtualBox);
            break;

        case kListUsbFilters:
            hrc = listUsbFilters(pVirtualBox);
            break;

        case kListSystemProperties:
            hrc = listSystemProperties(pVirtualBox);
            break;

#ifdef VBOX_WITH_UPDATE_AGENT
        case kListUpdateAgents:
            hrc = listUpdateAgents(pVirtualBox);
            break;
#endif
        case kListDhcpServers:
            hrc = listDhcpServers(pVirtualBox);
            break;

        case kListExtPacks:
            hrc = listExtensionPacks(pVirtualBox);
            break;

        case kListGroups:
            hrc = listGroups(pVirtualBox);
            break;

        case kListNatNetworks:
            hrc = listNATNetworks(fOptLong, fOptSorted, pVirtualBox);
            break;

        case kListVideoInputDevices:
            hrc = listVideoInputDevices(pVirtualBox);
            break;

        case kListScreenShotFormats:
            hrc = listScreenShotFormats(pVirtualBox);
            break;

        case kListCloudProviders:
            hrc = listCloudProviders(pVirtualBox);
            break;

        case kListCloudProfiles:
            hrc = listCloudProfiles(pVirtualBox, fOptLong);
            break;

        case kListCPUProfiles:
            hrc = listCPUProfiles(pVirtualBox, fOptLong, fOptSorted);
            break;

        case kListHostDrives:
            hrc = listHostDrives(pVirtualBox, fOptLong);
            break;

        case kListExecutionEngines:
            hrc = listExecutionEngines(pVirtualBox);
            break;

        /* No default here, want gcc warnings. */

    } /* end switch */

    return hrc;
}

/**
 * Handles the 'list' command.
 *
 * @returns Appropriate exit code.
 * @param   a                   Handler argument.
 */
RTEXITCODE handleList(HandlerArg *a)
{
    bool                   fOptLong        = false;
    bool                   fOptMultiple    = false;
    bool                   fOptSorted      = false;
    PlatformArchitecture_T enmPlatformArch = PlatformArchitecture_None;
    bool                   fFirst          = true;
    enum ListType_T        enmOptCommand   = kListNotSpecified;
    RTEXITCODE             rcExit          = RTEXITCODE_SUCCESS;

    static const RTGETOPTDEF s_aListOptions[] =
    {
        { "--long",             'l',                     RTGETOPT_REQ_NOTHING },
        { "--multiple",         'm',                     RTGETOPT_REQ_NOTHING }, /* not offical yet */
        { "--platform-arch",    'p',                     RTGETOPT_REQ_STRING  },
        { "--platform",         'p',                     RTGETOPT_REQ_STRING  }, /* shortcut for '--platform-arch' */
        { "--sorted",           's',                     RTGETOPT_REQ_NOTHING },
        { "vms",                kListVMs,                RTGETOPT_REQ_NOTHING },
        { "runningvms",         kListRunningVMs,         RTGETOPT_REQ_NOTHING },
        { "ostypes",            kListOsTypes,            RTGETOPT_REQ_NOTHING },
        { "ossubtypes",         kListOsSubtypes,         RTGETOPT_REQ_NOTHING },
        { "hostdvds",           kListHostDvds,           RTGETOPT_REQ_NOTHING },
        { "hostfloppies",       kListHostFloppies,       RTGETOPT_REQ_NOTHING },
        { "intnets",            kListInternalNetworks,   RTGETOPT_REQ_NOTHING },
        { "hostifs",            kListBridgedInterfaces,  RTGETOPT_REQ_NOTHING }, /* backward compatibility */
        { "bridgedifs",         kListBridgedInterfaces,  RTGETOPT_REQ_NOTHING },
#if defined(VBOX_WITH_NETFLT)
        { "hostonlyifs",        kListHostOnlyInterfaces, RTGETOPT_REQ_NOTHING },
#endif
#if defined(VBOX_WITH_VMNET)
        { "hostonlynets",       kListHostOnlyNetworks,   RTGETOPT_REQ_NOTHING },
#endif
#if defined(VBOX_WITH_CLOUD_NET)
        { "cloudnets",          kListCloudNetworks,      RTGETOPT_REQ_NOTHING },
#endif
        { "natnetworks",        kListNatNetworks,        RTGETOPT_REQ_NOTHING },
        { "natnets",            kListNatNetworks,        RTGETOPT_REQ_NOTHING },
        { "hostinfo",           kListHostInfo,           RTGETOPT_REQ_NOTHING },
        { "hostcpuids",         kListHostCpuIDs,         RTGETOPT_REQ_NOTHING },
        { "hddbackends",        kListHddBackends,        RTGETOPT_REQ_NOTHING },
        { "hdds",               kListHdds,               RTGETOPT_REQ_NOTHING },
        { "dvds",               kListDvds,               RTGETOPT_REQ_NOTHING },
        { "floppies",           kListFloppies,           RTGETOPT_REQ_NOTHING },
        { "usbhost",            kListUsbHost,            RTGETOPT_REQ_NOTHING },
        { "usbfilters",         kListUsbFilters,         RTGETOPT_REQ_NOTHING },
        { "systemproperties",   kListSystemProperties,   RTGETOPT_REQ_NOTHING },
#if defined(VBOX_WITH_UPDATE_AGENT)
        { "updates",            kListUpdateAgents,       RTGETOPT_REQ_NOTHING },
#endif
        { "dhcpservers",        kListDhcpServers,        RTGETOPT_REQ_NOTHING },
        { "extpacks",           kListExtPacks,           RTGETOPT_REQ_NOTHING },
        { "groups",             kListGroups,             RTGETOPT_REQ_NOTHING },
        { "webcams",            kListVideoInputDevices,  RTGETOPT_REQ_NOTHING },
        { "screenshotformats",  kListScreenShotFormats,  RTGETOPT_REQ_NOTHING },
        { "cloudproviders",     kListCloudProviders,     RTGETOPT_REQ_NOTHING },
        { "cloudprofiles",      kListCloudProfiles,      RTGETOPT_REQ_NOTHING },
        { "cpu-profiles",       kListCPUProfiles,        RTGETOPT_REQ_NOTHING },
        { "hostdrives",         kListHostDrives,         RTGETOPT_REQ_NOTHING },
        { "execution-engines",  kListExecutionEngines,   RTGETOPT_REQ_NOTHING },
    };

    int                 ch;
    RTGETOPTUNION       ValueUnion;
    RTGETOPTSTATE       GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aListOptions, RT_ELEMENTS(s_aListOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'l':  /* --long */
                fOptLong = true;
                break;

            case 'm':
                fOptMultiple = true;
                if (enmOptCommand == kListNotSpecified)
                    break;
                ch = enmOptCommand;
                RT_FALL_THRU();

            case 'p':  /* --platform[-arch] */
                enmPlatformArch = platformArchitectureToStr(ValueUnion.psz);
                if (enmPlatformArch == PlatformArchitecture_None)
                    return errorSyntax(List::tr("Invalid platform architecture specified"));
                break;

            case 's':
                fOptSorted = true;
                break;

            case kListVMs:
            case kListRunningVMs:
            case kListOsTypes:
            case kListOsSubtypes:
            case kListHostDvds:
            case kListHostFloppies:
            case kListInternalNetworks:
            case kListBridgedInterfaces:
#if defined(VBOX_WITH_NETFLT)
            case kListHostOnlyInterfaces:
#endif
#if defined(VBOX_WITH_VMNET)
            case kListHostOnlyNetworks:
#endif
#if defined(VBOX_WITH_CLOUD_NET)
            case kListCloudNetworks:
#endif
            case kListHostInfo:
            case kListHostCpuIDs:
            case kListHddBackends:
            case kListHdds:
            case kListDvds:
            case kListFloppies:
            case kListUsbHost:
            case kListUsbFilters:
            case kListSystemProperties:
#if defined(VBOX_WITH_UPDATE_AGENT)
            case kListUpdateAgents:
#endif
            case kListDhcpServers:
            case kListExtPacks:
            case kListGroups:
            case kListNatNetworks:
            case kListVideoInputDevices:
            case kListScreenShotFormats:
            case kListCloudProviders:
            case kListCloudProfiles:
            case kListCPUProfiles:
            case kListHostDrives:
            case kListExecutionEngines:
                enmOptCommand = (enum ListType_T)ch;
                if (fOptMultiple)
                {
                    if (fFirst)
                        fFirst = false;
                    else
                        RTPrintf("\n");
                    RTPrintf("[%s]\n", ValueUnion.pDef->pszLong);
                    HRESULT hrc = produceList(enmOptCommand, fOptLong, fOptSorted, enmPlatformArch, a->virtualBox);
                    if (FAILED(hrc))
                        rcExit = RTEXITCODE_FAILURE;
                }
                break;

            case VINF_GETOPT_NOT_OPTION:
                return errorSyntax(List::tr("Unknown subcommand \"%s\"."), ValueUnion.psz);

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    /*
     * If not in multiple list mode, we have to produce the list now.
     */
    if (enmOptCommand == kListNotSpecified)
        return errorSyntax(List::tr("Missing subcommand for \"list\" command.\n"));
    if (!fOptMultiple)
    {
        HRESULT hrc = produceList(enmOptCommand, fOptLong, fOptSorted, enmPlatformArch, a->virtualBox);
        if (FAILED(hrc))
            rcExit = RTEXITCODE_FAILURE;
    }

    return rcExit;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
