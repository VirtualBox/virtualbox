/* $Id: ovfreader.h 110133 2025-07-07 12:49:50Z brent.paulson@oracle.com $ */
/** @file
 * VirtualBox Main - OVF reader declarations.
 *
 * Depends only on IPRT, including the RTCString and IPRT XML classes.
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

#ifndef MAIN_INCLUDED_ovfreader_h
#define MAIN_INCLUDED_ovfreader_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "iprt/cpp/xml.h"
#include <map>
#include <vector>

namespace ovf
{

////////////////////////////////////////////////////////////////////////////////
//
// Errors
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Thrown by OVFReader for any kind of error that is not an XML error but
 * still makes the OVF impossible to parse. Based on xml::LogicError so
 * that one catch() for all xml::LogicError can handle all possible errors.
 */
class OVFLogicError : public xml::LogicError
{
public:
    OVFLogicError(const char *aFormat, ...);
};


////////////////////////////////////////////////////////////////////////////////
//
// Enumerations
//
////////////////////////////////////////////////////////////////////////////////

/**
 * CIM OS values.
 *
 * The OVF 1.10 spec refers to some CIM_OperatingSystem.mof doc.  Could this be it:
 *  http://cvs.opengroup.org/cgi-bin/cvsweb.cgi/pegasus/Schemas/CIM231/DMTF/System/CIM_OperatingSystem.mof
 *
 * @todo r=bird: Why are the values are repeating 'CIMOS'. CIMOSType_T is also
 *               repeating it self, 'Type' and '_T'. Why not call it kCIOMOpSys,
 *               easier to read as well.
 *               Then also apply: s/CIMOSType_CIMOS_/kCIMOpSys_/g
 */
enum CIMOSType_T
{
    CIMOSType_CIMOS_Unknown = 0,
    CIMOSType_CIMOS_Other   = 1,
    CIMOSType_CIMOS_MACOS   = 2,
    CIMOSType_CIMOS_ATTUNIX = 3,
    CIMOSType_CIMOS_DGUX    = 4,
    CIMOSType_CIMOS_DECNT   = 5,
    CIMOSType_CIMOS_Tru64UNIX   = 6,
    CIMOSType_CIMOS_OpenVMS = 7,
    CIMOSType_CIMOS_HPUX    = 8,
    CIMOSType_CIMOS_AIX = 9,
    CIMOSType_CIMOS_MVS = 10,
    CIMOSType_CIMOS_OS400   = 11,
    CIMOSType_CIMOS_OS2 = 12,
    CIMOSType_CIMOS_JavaVM  = 13,
    CIMOSType_CIMOS_MSDOS   = 14,
    CIMOSType_CIMOS_WIN3x   = 15,
    CIMOSType_CIMOS_WIN95   = 16,
    CIMOSType_CIMOS_WIN98   = 17,
    CIMOSType_CIMOS_WINNT   = 18,
    CIMOSType_CIMOS_WINCE   = 19,
    CIMOSType_CIMOS_NCR3000 = 20,
    CIMOSType_CIMOS_NetWare = 21,
    CIMOSType_CIMOS_OSF = 22,
    CIMOSType_CIMOS_DCOS    = 23,
    CIMOSType_CIMOS_ReliantUNIX = 24,
    CIMOSType_CIMOS_SCOUnixWare = 25,
    CIMOSType_CIMOS_SCOOpenServer   = 26,
    CIMOSType_CIMOS_Sequent = 27,
    CIMOSType_CIMOS_IRIX    = 28,
    CIMOSType_CIMOS_Solaris = 29,
    CIMOSType_CIMOS_SunOS   = 30,
    CIMOSType_CIMOS_U6000   = 31,
    CIMOSType_CIMOS_ASERIES = 32,
    CIMOSType_CIMOS_HPNonStopOS = 33,
    CIMOSType_CIMOS_HPNonStopOSS    = 34,
    CIMOSType_CIMOS_BS2000  = 35,
    CIMOSType_CIMOS_LINUX   = 36,
    CIMOSType_CIMOS_Lynx    = 37,
    CIMOSType_CIMOS_XENIX   = 38,
    CIMOSType_CIMOS_VM  = 39,
    CIMOSType_CIMOS_InteractiveUNIX = 40,
    CIMOSType_CIMOS_BSDUNIX = 41,
    CIMOSType_CIMOS_FreeBSD = 42,
    CIMOSType_CIMOS_NetBSD  = 43,
    CIMOSType_CIMOS_GNUHurd = 44,
    CIMOSType_CIMOS_OS9 = 45,
    CIMOSType_CIMOS_MACHKernel  = 46,
    CIMOSType_CIMOS_Inferno = 47,
    CIMOSType_CIMOS_QNX = 48,
    CIMOSType_CIMOS_EPOC    = 49,
    CIMOSType_CIMOS_IxWorks = 50,
    CIMOSType_CIMOS_VxWorks = 51,
    CIMOSType_CIMOS_MiNT    = 52,
    CIMOSType_CIMOS_BeOS    = 53,
    CIMOSType_CIMOS_HPMPE   = 54,
    CIMOSType_CIMOS_NextStep    = 55,
    CIMOSType_CIMOS_PalmPilot   = 56,
    CIMOSType_CIMOS_Rhapsody    = 57,
    CIMOSType_CIMOS_Windows2000 = 58,
    CIMOSType_CIMOS_Dedicated   = 59,
    CIMOSType_CIMOS_OS390   = 60,
    CIMOSType_CIMOS_VSE = 61,
    CIMOSType_CIMOS_TPF = 62,
    CIMOSType_CIMOS_WindowsMe   = 63,
    CIMOSType_CIMOS_CalderaOpenUNIX = 64,
    CIMOSType_CIMOS_OpenBSD = 65,
    CIMOSType_CIMOS_NotApplicable   = 66,
    CIMOSType_CIMOS_WindowsXP   = 67,
    CIMOSType_CIMOS_zOS = 68,
    CIMOSType_CIMOS_MicrosoftWindowsServer2003  = 69,
    CIMOSType_CIMOS_MicrosoftWindowsServer2003_64   = 70,
    CIMOSType_CIMOS_WindowsXP_64    = 71,
    CIMOSType_CIMOS_WindowsXPEmbedded   = 72,
    CIMOSType_CIMOS_WindowsVista    = 73,
    CIMOSType_CIMOS_WindowsVista_64 = 74,
    CIMOSType_CIMOS_WindowsEmbeddedforPointofService    = 75,
    CIMOSType_CIMOS_MicrosoftWindowsServer2008  = 76,
    CIMOSType_CIMOS_MicrosoftWindowsServer2008_64   = 77,
    CIMOSType_CIMOS_FreeBSD_64  = 78,
    CIMOSType_CIMOS_RedHatEnterpriseLinux   = 79,
    CIMOSType_CIMOS_RedHatEnterpriseLinux_64    = 80,
    CIMOSType_CIMOS_Solaris_64  = 81,
    CIMOSType_CIMOS_SUSE    = 82,
    CIMOSType_CIMOS_SUSE_64 = 83,
    CIMOSType_CIMOS_SLES    = 84,
    CIMOSType_CIMOS_SLES_64 = 85,
    CIMOSType_CIMOS_NovellOES   = 86,
    CIMOSType_CIMOS_NovellLinuxDesktop  = 87,
    CIMOSType_CIMOS_SunJavaDesktopSystem    = 88,
    CIMOSType_CIMOS_Mandriva    = 89,
    CIMOSType_CIMOS_Mandriva_64 = 90,
    CIMOSType_CIMOS_TurboLinux  = 91,
    CIMOSType_CIMOS_TurboLinux_64   = 92,
    CIMOSType_CIMOS_Ubuntu  = 93,
    CIMOSType_CIMOS_Ubuntu_64   = 94,
    CIMOSType_CIMOS_Debian  = 95,
    CIMOSType_CIMOS_Debian_64   = 96,
    CIMOSType_CIMOS_Linux_2_4_x = 97,
    CIMOSType_CIMOS_Linux_2_4_x_64  = 98,
    CIMOSType_CIMOS_Linux_2_6_x = 99,
    CIMOSType_CIMOS_Linux_2_6_x_64  = 100,
    CIMOSType_CIMOS_Linux_64    = 101,
    CIMOSType_CIMOS_Other_64    = 102,
    // types added with CIM 2.25.0 follow:
    CIMOSType_CIMOS_WindowsServer2008R2 = 103,
    CIMOSType_CIMOS_VMwareESXi = 104,
    CIMOSType_CIMOS_Windows7 = 105,
    CIMOSType_CIMOS_CentOS = 106,
    CIMOSType_CIMOS_CentOS_64 = 107,
    CIMOSType_CIMOS_OracleLinux = 108,
    CIMOSType_CIMOS_OracleLinux_64 = 109,
    CIMOSType_CIMOS_eComStation = 110,
    // no new types added with CIM 2.26.0
    CIMOSType_CIMOS_WindowsServer2011 = 111,
    CIMOSType_CIMOS_WindowsServer2012 = 112,
    CIMOSType_CIMOS_Windows8 = 113,
    CIMOSType_CIMOS_Windows8_64 = 114,
    CIMOSType_CIMOS_WindowsServer2012R2 = 115,
    CIMOSType_CIMOS_Windows8_1 = 116,
    CIMOSType_CIMOS_Windows8_1_64 = 117,
    CIMOSType_CIMOS_WindowsServer2016 = 118,
    CIMOSType_CIMOS_Windows10 = 119,
    CIMOSType_CIMOS_Windows10_64 = 120,
    // the above covers up to CIM 2.52.0, without checking when it was added
};

enum OVFVersion_T
{
    OVFVersion_unknown,
    OVFVersion_0_9,
    OVFVersion_1_0,
    OVFVersion_2_0
};

const char* const OVF09_URI_string = "http://www.vmware.com/schema/ovf/1/envelope";
const char* const OVF10_URI_string = "http://schemas.dmtf.org/ovf/envelope/1";
const char* const OVF20_URI_string = "http://schemas.dmtf.org/ovf/envelope/2";

const char* const DTMF_SPECS_URI = "http://schemas.dmtf.org/wbem/cim-html/2/";

////////////////////////////////////////////////////////////////////////////////
//
// Envelope data
//
////////////////////////////////////////////////////////////////////////////////
struct EnvelopeData
{
    OVFVersion_T version;//OVF standard version, it is used internally only by VirtualBox
    RTCString lang;//language

    OVFVersion_T getOVFVersion() const
    {
            return version;
    }


    RTCString getStringOVFVersion() const
    {
        if (version == OVFVersion_0_9)
            return "0.9";
        else if (version == OVFVersion_1_0)
            return "1.0";
        else if (version == OVFVersion_2_0)
            return "2.0";
        else
            return "";
    }

    void setOVFVersion(OVFVersion_T v)
    {
        version = v;
    }
};


struct FileReference
{
    RTCString strHref;       // value from /References/File/@href (filename)
    RTCString strDiskId;     // value from /References/File/@id ()
};

typedef std::map<uint32_t, FileReference> FileReferenceMap;

////////////////////////////////////////////////////////////////////////////////
//
// Hardware definition structs
//
////////////////////////////////////////////////////////////////////////////////

struct DiskImage
{
    // fields from /DiskSection/Disk
    RTCString strDiskId;     // value from DiskSection/Disk/@diskId
    int64_t iCapacity;              // value from DiskSection/Disk/@capacity;
                                    // (maximum size for dynamic images, I guess; we always translate this to bytes)
    int64_t iPopulatedSize;         // optional value from DiskSection/Disk/@populatedSize
                                    // (actual used size of disk, always in bytes; can be an estimate of used disk
                                    // space, but cannot be larger than iCapacity; -1 if not set)
    RTCString strFormat;              // value from DiskSection/Disk/@format
                // typically http://www.vmware.com/interfaces/specifications/vmdk.html#streamOptimized
    RTCString uuidVBox;      // optional; if the file was exported by VirtualBox >= 3.2,
                                    // then this has the UUID with which the disk was registered

    // fields from /References/File; the spec says the file reference from disk can be empty,
    // so in that case, strFilename will be empty, then a new disk should be created
    RTCString strHref;       // value from /References/File/@href (filename); if empty, then the remaining fields are ignored
    int64_t iSize;                  // value from /References/File/@size (optional according to spec; then we set -1 here)
    int64_t iChunkSize;             // value from /References/File/@chunkSize (optional, unsupported)
    RTCString strCompression; // value from /References/File/@compression (optional, can be "gzip" according to spec)

    // additional field which has a descriptive size in megabytes derived from the above; this can be used for progress reports
    uint32_t ulSuggestedSizeMB;
};

enum ResourceType_T
{
    ResourceType_Invalid  = 0,
    ResourceType_Other  = 1,
    ResourceType_ComputerSystem = 2,
    ResourceType_Processor  = 3,
    ResourceType_Memory = 4,
    ResourceType_IDEController  = 5,
    ResourceType_ParallelSCSIHBA    = 6,
    ResourceType_FCHBA  = 7,
    ResourceType_iSCSIHBA   = 8,
    ResourceType_IBHCA  = 9,
    ResourceType_EthernetAdapter    = 10,
    ResourceType_OtherNetworkAdapter    = 11,
    ResourceType_IOSlot = 12,
    ResourceType_IODevice   = 13,
    ResourceType_FloppyDrive    = 14,
    ResourceType_CDDrive    = 15,
    ResourceType_DVDDrive   = 16,
    ResourceType_HardDisk   = 17,
    ResourceType_TapeDrive  = 18,
    ResourceType_StorageExtent  = 19,
    ResourceType_OtherStorageDevice  = 20,
    ResourceType_SerialPort = 21,
    ResourceType_ParallelPort   = 22,
    ResourceType_USBController  = 23,
    ResourceType_GraphicsController = 24,
    ResourceType_IEEE1394Controller = 25,
    ResourceType_PartitionableUnit  = 26,
    ResourceType_BasePartitionableUnit  = 27,
    ResourceType_Power  = 28,
    ResourceType_CoolingCapacity    = 29,
    ResourceType_EthernetSwitchPort = 30,
    ResourceType_LogicalDisk    = 31,
    ResourceType_StorageVolume  = 32,
    ResourceType_EthernetConnection = 33,
    ResourceType_SoundCard  = 35,   /**< @todo r=klaus: Not part of OVF/CIM spec, should use "Other" or some value from 0x8000..0xffff. */
    ResourceType_NVRAM  = 0x8000
};


enum StorageAccessType_T
{   StorageAccessType_Unknown = 0,
    StorageAccessType_Readable = 1,
    StorageAccessType_Writeable = 2,
    StorageAccessType_ReadWrite = 3
};

enum ComplianceType_T
{   ComplianceType_No = 0,
    ComplianceType_Soft = 1,
    ComplianceType_Medium = 2,
    ComplianceType_Strong = 3
};

class VirtualHardwareItem
{
public:
    RTCString strDescription;
    RTCString strCaption;
    RTCString strElementName;

    RTCString strInstanceID;
    RTCString strParent;

    ResourceType_T resourceType;
    RTCString strOtherResourceType;
    RTCString strResourceSubType;
    bool fResourceRequired;

    RTCString strHostResource;   ///< "Abstractly specifies how a device shall connect to a resource on the deployment platform.
                                 /// Not all devices need a backing." Used with disk items, for which this
                                 /// references a virtual disk from the Disks section.
    bool fAutomaticAllocation;
    bool fAutomaticDeallocation;
    RTCString strConnection;     ///< "All Ethernet adapters that specify the same abstract network connection name within an OVF
                                 /// package shall be deployed on the same network. The abstract network connection name shall be
                                 /// listed in the NetworkSection at the outermost envelope level." We ignore this and only set up
                                 /// a network adapter depending on the network name.
    RTCString strAddress;        ///< "Device-specific. For an Ethernet adapter, this specifies the MAC address."
    int32_t lAddress;            ///< strAddress as an integer, if applicable.
    RTCString strAddressOnParent;///< "For a device, this specifies its location on the controller."
    RTCString strAllocationUnits;///< "Specifies the units of allocation used. For example, “byte * 2^20”."
    uint64_t ullVirtualQuantity; ///< "Specifies the quantity of resources presented. For example, “256”."
    uint64_t ullReservation;     ///< "Specifies the minimum quantity of resources guaranteed to be available."
    uint64_t ullLimit;           ///< "Specifies the maximum quantity of resources that will be granted."
    uint64_t ullWeight;          ///< "Specifies a relative priority for this allocation in relation to other allocations."

    RTCString strConsumerVisibility;
    RTCString strMappingBehavior;
    RTCString strPoolID;
    uint32_t ulBusNumber;        ///< seen with IDE controllers, but not listed in OVF spec

    int m_iLineNumber;           ///< line number of \<Item\> element in XML source; cached for error messages

    VirtualHardwareItem()
        : resourceType(ResourceType_Invalid)
        , fResourceRequired(false)
        , fAutomaticAllocation(false)
        , fAutomaticDeallocation(false)
        , lAddress(0)
        , ullVirtualQuantity(0)
        , ullReservation(0)
        , ullLimit(0)
        , ullWeight(0)
        , ulBusNumber(0)
        , m_iLineNumber(0)
        , fDefault(false)
    {
        itemName = "Item";
    }

    virtual ~VirtualHardwareItem() { /* Makes MSC happy. */ }

    void fillItem(const xml::ElementNode *item);

    void setDefaultFlag()
    {
        fDefault = true;
    }

    bool isThereDefaultValues() const
    {
        return fDefault;
    }

    void checkConsistencyAndCompliance() RT_THROW(OVFLogicError)
    {
        _checkConsistencyAndCompliance();
    }

protected:
    virtual void _checkConsistencyAndCompliance() RT_THROW(OVFLogicError);
    virtual const RTCString& getItemName()
    {
        return _getItemName();
    }

private:
    RTCString itemName;
    bool fDefault;//true means that some fields were absent in the XML and some default values were assigned to.

    virtual const RTCString& _getItemName()
    {
        return itemName;
    }
};

class StorageItem: public VirtualHardwareItem
{
    //see DMTF Schema Documentation http://schemas.dmtf.org/wbem/cim-html/2/
    StorageAccessType_T accessType;
    RTCString strHostExtentName;
#if 0 /* unused */
    int16_t hostExtentNameFormat;
    int16_t hostExtentNameNamespace;
    int64_t hostExtentStartingAddress;
#endif
    int64_t hostResourceBlockSize;
    int64_t limit;
    RTCString strOtherHostExtentNameFormat;
    RTCString strOtherHostExtentNameNamespace;
    int64_t reservation;
    int64_t virtualQuantity;
    RTCString strVirtualQuantityUnits;
    int64_t virtualResourceBlockSize;

public:
    StorageItem()
        : VirtualHardwareItem()
        , accessType(StorageAccessType_Unknown)
#if 0 /* unused */
        , hostExtentNameFormat(-1)
        , hostExtentNameNamespace(-1)
        , hostExtentStartingAddress(-1)
#endif
        , hostResourceBlockSize(-1)
        , limit(-1)
        , reservation(-1)
        , virtualQuantity(-1)
        , virtualResourceBlockSize(-1)
    {
        itemName = "StorageItem";
    };

    void fillItem(const xml::ElementNode *item);

protected:
    virtual void _checkConsistencyAndCompliance() RT_THROW(OVFLogicError);
private:
    RTCString itemName;

    virtual const RTCString& _getItemName()
    {
        return itemName;
    }
};


class EthernetPortItem: public VirtualHardwareItem
{
    //see DMTF Schema Documentation http://schemas.dmtf.org/wbem/cim-html/2/
#if 0 /* unused */
    uint16_t DefaultPortVID;
    uint16_t DefaultPriority;
    uint16_t DesiredVLANEndpointMode;
    uint32_t GroupID;
    uint32_t ManagerID;
    uint16_t NetworkPortProfileIDType;
#endif
    RTCString strNetworkPortProfileID;
    RTCString strOtherEndpointMode;
    RTCString strOtherNetworkPortProfileIDTypeInfo;
    RTCString strPortCorrelationID;
#if 0 /* unused */
    uint16_t PortVID;
    bool Promiscuous;
    uint64_t ReceiveBandwidthLimit;
    uint16_t ReceiveBandwidthReservation;
    bool SourceMACFilteringEnabled;
    uint32_t VSITypeID;
    uint8_t VSITypeIDVersion;
    uint16_t AllowedPriorities[256];
    uint16_t AllowedToReceiveVLANs[256];
    uint16_t AllowedToTransmitVLANs[256];
#endif
    RTCString strAllowedToReceiveMACAddresses;
    RTCString strAllowedToTransmitMACAddresses;

public:
    EthernetPortItem() : VirtualHardwareItem()
    {
        itemName = "EthernetPortItem";
    };

    void fillItem(const xml::ElementNode *item);

protected:
    virtual void _checkConsistencyAndCompliance() RT_THROW(OVFLogicError);
private:
    RTCString itemName;

    virtual const RTCString &_getItemName()
    {
        return itemName;
    }
};

typedef std::map<RTCString, DiskImage> DiskImagesMap;

struct VirtualSystem;


/**
 * VirtualHardwareItem pointer vector with safe cleanup.
 *
 * We need to use object pointers because we also want EthernetPortItem and
 * StorageItems to go into the container.
 */
class HardwareItemVector : public std::vector<VirtualHardwareItem *>
{
public:
    HardwareItemVector() : std::vector<VirtualHardwareItem *>() { }
    ~HardwareItemVector()
    {
        for (iterator it = begin(); it != end(); ++it)
            delete(*it);
        clear();
    }

    /* We only support copying if the vector is empty, we'd need something like
       shared_ptr for dealing with a populated list. (Problem found by VS 2022.) */
    HardwareItemVector(const HardwareItemVector &a_rThat)
        : std::vector<VirtualHardwareItem *>()
    {
        Assert(a_rThat.begin() == a_rThat.end());
        RT_NOREF(a_rThat);
    }

private:
#if RT_CPLUSPLUS_PREREQ(201100) /* VC2022: No copy assignment operator (see copy constructor). */
    HardwareItemVector &operator=(const HardwareItemVector &) = delete;
#else
    HardwareItemVector &operator=(const HardwareItemVector &);
#endif
};

struct HardDiskController
{
    RTCString               strIdController;    // instance ID (Item/InstanceId); this gets referenced from VirtualDisk

    enum ControllerSystemType { IDE, SATA, SCSI, VIRTIOSCSI, NVMe };
    ControllerSystemType    system;             // one of IDE, SATA, SCSI, VIRTIOSCSI, NVMe

    RTCString               strControllerType;
            // controller subtype (Item/ResourceSubType); e.g. "LsiLogic"; can be empty (esp. for IDE)
            // note that we treat LsiLogicSAS as a SCSI controller (system == SCSI) even though VirtualBox
            // treats it as a fourth class besides IDE, SATA, SCSI

    int32_t                 lAddress;           // value from OVF "Address" element
    bool                    fPrimary;           // controller index; this is determined heuristically by the OVF reader and will
                                                // be true for the first controller of this type (e.g. IDE primary ctler) or
                                                // false for the next (e.g. IDE secondary ctler)

    HardDiskController()
        : system(IDE),
          lAddress(0),
          fPrimary(true)
    { }
};

typedef std::map<RTCString, HardDiskController> ControllersMap;

struct VirtualDisk
{
    RTCString   strIdController;    // SCSI (or IDE) controller this disk is connected to;
                                    // this must match HardDiskController.strIdController and
                                    // points into VirtualSystem.mapControllers
    uint32_t    ulAddressOnParent;  // parsed strAddressOnParent of hardware item; will be 0 or 1 for IDE
                                    // and possibly higher for disks attached to SCSI controllers (untested)
    RTCString   strDiskId;          // if the hard disk has an ovf:/disk/<id> reference,
                                    // this receives the <id> component; points to one of the
                                    // references in Appliance::Data.mapDisks
    bool        fEmpty; //true - empty disk, e.g. the component <rasd:HostResource>...</rasd:HostResource> is absent.
};

typedef std::map<RTCString, VirtualDisk> VirtualDisksMap;

/**
 * A list of EthernetAdapters is contained in VirtualSystem, representing the
 * ethernet adapters in the virtual system.
 */
struct EthernetAdapter
{
    RTCString    strAdapterType;         // "PCNet32" or "E1000" or whatever; from <rasd:ResourceSubType>
    RTCString    strNetworkName;         // from <rasd:Connection>
};

typedef std::list<EthernetAdapter> EthernetAdaptersList;

/**
 * A list of VirtualSystem structs is created by OVFReader::read(). Each refers to
 * a \<VirtualSystem\> block in the OVF file.
 */
struct VirtualSystem
{
    RTCString    strName;                // copy of VirtualSystem/@id

    RTCString    strDescription;         // copy of VirtualSystem/AnnotationSection content, if any

    CIMOSType_T  cimos;
    RTCString    strCimosDesc;           // readable description of the cimos type in the case of cimos = 0/1/102
    RTCString    strTypeVBox;            // optional type from @vbox:ostype attribute (VirtualBox 4.0 or higher)

    RTCString    strVirtualSystemType;   // generic hardware description; OVF says this can be something like "vmx-4" or "xen";
                                                // VMware Workstation 6.5 is "vmx-07"

    HardwareItemVector  vecHardwareItems;       //< vector containing all virtual hardware items in parsing order.

    uint64_t            ullMemorySize;          // always in bytes, copied from llHardwareItems; default = 0 (unspecified)
    uint16_t            cCPUs;                  // no. of CPUs, copied from llHardwareItems; default = 1

    EthernetAdaptersList llEthernetAdapters;    // (one for each VirtualSystem/Item[@ResourceType=10]element)

    ControllersMap      mapControllers;
            // list of hard disk controllers
            // (one for each VirtualSystem/Item[@ResourceType=6] element with accumulated data from children)

    VirtualDisksMap     mapVirtualDisks;
            // (one for each VirtualSystem/Item[@ResourceType=17] element with accumulated data from children)

    bool                fHasFloppyDrive;        // true if there's a floppy item in mapHardwareItems
    bool                fHasCdromDrive;         // true if there's a CD-ROM item in mapHardwareItems; ISO images are not yet supported by OVFtool
    bool                fHasUsbController;      // true if there's a USB controller item in mapHardwareItems

    RTCString    strSoundCardType;       // if not empty, then the system wants a soundcard; this then specifies the hardware;
                                                // VMware Workstation 6.5 uses "ensoniq1371" for example

    RTCString    strLicenseText;         // license info if any; receives contents of VirtualSystem/EulaSection/License

    RTCString    strProduct;             // product info if any; receives contents of VirtualSystem/ProductSection/Product
    RTCString    strVendor;              // product info if any; receives contents of VirtualSystem/ProductSection/Vendor
    RTCString    strVersion;             // product info if any; receives contents of VirtualSystem/ProductSection/Version
    RTCString    strProductUrl;          // product info if any; receives contents of VirtualSystem/ProductSection/ProductUrl
    RTCString    strVendorUrl;           // product info if any; receives contents of VirtualSystem/ProductSection/VendorUrl

    RTCString    strNvramPath;           // path to the VM's NVRAM path, if present

    const xml::ElementNode *pelmVBoxMachine; // pointer to <vbox:Machine> element under <VirtualSystem> element or NULL if not present

    VirtualSystem()
        : cimos(CIMOSType_CIMOS_Unknown)
        , ullMemorySize(0)
        , cCPUs(1)
        , fHasFloppyDrive(false)
        , fHasCdromDrive(false)
        , fHasUsbController(false)
        , pelmVBoxMachine(NULL)
    {
    }

#if RT_CPLUSPLUS_PREREQ(201100) /* VC2022: Excplit default copy constructor and copy assignment operator to avoid warnings. */
    /* Note! HardwareItemVector only allow copying if it's empty. */
    VirtualSystem(VirtualSystem const &) = default;
    VirtualSystem &operator=(VirtualSystem const &) = delete;
#endif
};

////////////////////////////////////////////////////////////////////////////////
//
// Class OVFReader
//
////////////////////////////////////////////////////////////////////////////////

/**
 * OVFReader attempts to open, read in and parse an OVF XML file. This is all done
 * in the constructor; if there is any kind of error in the file -- filesystem error
 * from IPRT, XML parsing errors from libxml, or OVF logical errors --, exceptions
 * are thrown. These are all based on xml::Error.
 *
 * Hence, use this class as follows:
<code>
    OVFReader *pReader = NULL;
    try
    {
        pReader = new("/path/to/file.ovf");
    }
    catch (xml::Error &e)
    {
        printf("A terrible thing happened: %s", e.what());
    }
    // now go look at pReader->m_llVirtualSystem and what's in there
    if (pReader)
        delete pReader;
</code>
 */
class OVFReader
{
public:
    OVFReader();
    OVFReader(const void *pvBuf, size_t cbSize, const RTCString &path);
    OVFReader(const RTCString &path);

    // Data fields
    EnvelopeData                m_envelopeData;       //data of root element "Envelope"
    RTCString                   m_strPath;            // file name given to constructor
    DiskImagesMap               m_mapDisks;           // map of DiskImage structs, sorted by DiskImage.strDiskId
    std::list<VirtualSystem>    m_llVirtualSystems;   // list of virtual systems, created by and valid after read()

private:
    xml::Document               m_doc;

    void parse();
    void LoopThruSections(const xml::ElementNode *pReferencesElem, const xml::ElementNode *pCurElem);
    void HandleDiskSection(const xml::ElementNode *pReferencesElem, const xml::ElementNode *pSectionElem);
    void HandleNetworkSection(const xml::ElementNode *pSectionElem);
    void HandleVirtualSystemContent(const xml::ElementNode *pReferencesElem, const xml::ElementNode *pContentElem);
};

} // end namespace ovf

#endif /* !MAIN_INCLUDED_ovfreader_h */

