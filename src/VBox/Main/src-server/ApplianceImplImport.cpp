/* $Id: ApplianceImplImport.cpp 110133 2025-07-07 12:49:50Z brent.paulson@oracle.com $ */
/** @file
 * IAppliance and IVirtualSystem COM class implementations.
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

#define LOG_GROUP LOG_GROUP_MAIN_APPLIANCE
#include <iprt/alloca.h>
#include <iprt/path.h>
#include <iprt/cpp/path.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/sha.h>
#include <iprt/manifest.h>
#include <iprt/zip.h>
#include <iprt/stream.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/pkix.h>
#include <iprt/crypto/store.h>
#include <iprt/crypto/x509.h>
#include <iprt/rand.h>

#include <iprt/formats/tar.h>

#include <VBox/vd.h>
#include <VBox/com/array.h>

#include "ApplianceImpl.h"
#include "VirtualBoxImpl.h"
#include "GuestOSTypeImpl.h"
#include "ProgressImpl.h"
#include "MachineImpl.h"
#include "MediumImpl.h"
#include "MediumFormatImpl.h"
#include "SystemPropertiesImpl.h"
#include "HostImpl.h"

#include "AutoCaller.h"
#include "LoggingNew.h"

#include "ApplianceImplPrivate.h"
#include "CertificateImpl.h"
#include "ovfreader.h"

#include <VBox/param.h>
#include <VBox/version.h>
#include <VBox/settings.h>

#include <set>

using namespace std;

////////////////////////////////////////////////////////////////////////////////
//
// IAppliance public methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Public method implementation. This opens the OVF with ovfreader.cpp.
 * Thread implementation is in Appliance::readImpl().
 *
 * @param aFile     File to read the appliance from.
 * @param aProgress Progress object.
 * @return
 */
HRESULT Appliance::read(const com::Utf8Str &aFile,
                        ComPtr<IProgress> &aProgress)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!i_isApplianceIdle())
        return E_ACCESSDENIED;

    if (m->pReader)
    {
        delete m->pReader;
        m->pReader = NULL;
    }

    /* Parse all necessary info out of the URI (please not how stupid utterly wasteful
       this status & allocation error throwing is): */
    try
    {
        i_parseURI(aFile, m->locInfo); /* may throw hrc. */
    }
    catch (HRESULT hrcXcpt)
    {
        return hrcXcpt;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    // see if we can handle this file; for now we insist it has an ovf/ova extension
    if (   m->locInfo.storageType == VFSType_File
        && !aFile.endsWith(".ovf", Utf8Str::CaseInsensitive)
        && !aFile.endsWith(".ova", Utf8Str::CaseInsensitive))
        return setError(VBOX_E_FILE_ERROR, tr("Appliance file must have .ovf or .ova extension"));

    ComObjPtr<Progress> progress;
    HRESULT hrc = i_readImpl(m->locInfo, progress);
    if (SUCCEEDED(hrc))
        progress.queryInterfaceTo(aProgress.asOutParam());
    return hrc;
}

/**
 * Public method implementation. This looks at the output of ovfreader.cpp and creates
 * VirtualSystemDescription instances.
 * @return
 */
HRESULT Appliance::interpret()
{
    /// @todo
    //  - don't use COM methods but the methods directly (faster, but needs appropriate
    // locking of that objects itself (s. HardDisk))
    //  - Appropriate handle errors like not supported file formats
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!i_isApplianceIdle())
        return E_ACCESSDENIED;

    HRESULT hrc = S_OK;

    /* Clear any previous virtual system descriptions */
    m->virtualSystemDescriptions.clear();

    if (m->locInfo.storageType == VFSType_File && !m->pReader)
        return setError(E_FAIL,
                        tr("Cannot interpret appliance without reading it first (call read() before interpret())"));

    // Change the appliance state so we can safely leave the lock while doing time-consuming
    // medium imports; also the below method calls do all kinds of locking which conflicts with
    // the appliance object lock
    m->state = ApplianceImporting;
    alock.release();

    /* Try/catch so we can clean up on error */
    try
    {
        list<ovf::VirtualSystem>::const_iterator it;
        /* Iterate through all virtual systems */
        for (it = m->pReader->m_llVirtualSystems.begin();
             it != m->pReader->m_llVirtualSystems.end();
             ++it)
        {
            const ovf::VirtualSystem &vsysThis = *it;

            ComObjPtr<VirtualSystemDescription> pNewDesc;
            hrc = pNewDesc.createObject();
            if (FAILED(hrc)) throw hrc;
            hrc = pNewDesc->init();
            if (FAILED(hrc)) throw hrc;

            // if the virtual system in OVF had a <vbox:Machine> element, have the
            // VirtualBox settings code parse that XML now
            if (vsysThis.pelmVBoxMachine)
                pNewDesc->i_importVBoxMachineXML(*vsysThis.pelmVBoxMachine);

            // Guest OS type
            // This is taken from one of three places, in this order:
            Utf8Str strOsTypeVBox;
            Utf8StrFmt strCIMOSType("%RU32", (uint32_t)vsysThis.cimos);
            // 1) If there is a <vbox:Machine>, then use the type from there.
            if (   vsysThis.pelmVBoxMachine
                && pNewDesc->m->pConfig->machineUserData.strOsType.isNotEmpty()
               )
                strOsTypeVBox = pNewDesc->m->pConfig->machineUserData.strOsType;
            // 2) Otherwise, if there is OperatingSystemSection/vbox:OSType, use that one.
            else if (vsysThis.strTypeVBox.isNotEmpty())      // OVFReader has found vbox:OSType
                strOsTypeVBox = vsysThis.strTypeVBox;
            // 3) Otherwise, make a best guess what the vbox type is from the OVF (CIM) OS type.
            else
                convertCIMOSType2VBoxOSType(strOsTypeVBox, vsysThis.cimos, vsysThis.strCimosDesc);
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_OS,
                                 "",
                                 strCIMOSType,
                                 strOsTypeVBox);

            /* VM name */
            Utf8Str nameVBox;
            /* If there is a <vbox:Machine>, we always prefer the setting from there. */
            if (   vsysThis.pelmVBoxMachine
                && pNewDesc->m->pConfig->machineUserData.strName.isNotEmpty())
                nameVBox = pNewDesc->m->pConfig->machineUserData.strName;
            else
                nameVBox = vsysThis.strName;
            /* If there isn't any name specified create a default one out
             * of the OS type */
            if (nameVBox.isEmpty())
                nameVBox = strOsTypeVBox;
            i_searchUniqueVMName(nameVBox);
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_Name,
                                 "",
                                 vsysThis.strName,
                                 nameVBox);

            /* VM Primary Group */
            Utf8Str strPrimaryGroup;
            if (   vsysThis.pelmVBoxMachine
                && pNewDesc->m->pConfig->machineUserData.llGroups.size())
                strPrimaryGroup = pNewDesc->m->pConfig->machineUserData.llGroups.front();
            if (strPrimaryGroup.isEmpty())
                strPrimaryGroup = "/";
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_PrimaryGroup,
                                 "",
                                 "" /* no direct OVF correspondence */,
                                 strPrimaryGroup);

            /* Based on the VM name, create a target machine path. */
            Bstr bstrSettingsFilename;
            hrc = mVirtualBox->ComposeMachineFilename(Bstr(nameVBox).raw(),
                                                      Bstr(strPrimaryGroup).raw(),
                                                      NULL /* aCreateFlags */,
                                                      NULL /* aBaseFolder */,
                                                      bstrSettingsFilename.asOutParam());
            if (FAILED(hrc)) throw hrc;
            Utf8Str strMachineFolder(bstrSettingsFilename);
            strMachineFolder.stripFilename();

#if 1
            /* The import logic should work exactly the same whether the
             * following 2 items are present or not, but of course it may have
             * an influence on the exact presentation of the import settings
             * of an API client. */
            Utf8Str strSettingsFilename(bstrSettingsFilename);
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_SettingsFile,
                                 "",
                                 "" /* no direct OVF correspondence */,
                                 strSettingsFilename);
            Utf8Str strBaseFolder;
            mVirtualBox->i_getDefaultMachineFolder(strBaseFolder);
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_BaseFolder,
                                 "",
                                 "" /* no direct OVF correspondence */,
                                 strBaseFolder);
#endif

            /* VM Product */
            if (!vsysThis.strProduct.isEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_Product,
                                      "",
                                      vsysThis.strProduct,
                                      vsysThis.strProduct);

            /* VM Vendor */
            if (!vsysThis.strVendor.isEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_Vendor,
                                     "",
                                     vsysThis.strVendor,
                                     vsysThis.strVendor);

            /* VM Version */
            if (!vsysThis.strVersion.isEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_Version,
                                     "",
                                     vsysThis.strVersion,
                                     vsysThis.strVersion);

            /* VM ProductUrl */
            if (!vsysThis.strProductUrl.isEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_ProductUrl,
                                     "",
                                     vsysThis.strProductUrl,
                                     vsysThis.strProductUrl);

            /* VM VendorUrl */
            if (!vsysThis.strVendorUrl.isEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_VendorUrl,
                                     "",
                                     vsysThis.strVendorUrl,
                                     vsysThis.strVendorUrl);

            /* VM description */
            if (!vsysThis.strDescription.isEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_Description,
                                     "",
                                     vsysThis.strDescription,
                                     vsysThis.strDescription);

            /* VM license */
            if (!vsysThis.strLicenseText.isEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_License,
                                     "",
                                     vsysThis.strLicenseText,
                                     vsysThis.strLicenseText);

            /* Now that we know the OS type, get our internal defaults based on
             * that, if it is known (otherwise pGuestOSType will be NULL). */
            ComPtr<IGuestOSType> pGuestOSType;
            mVirtualBox->GetGuestOSType(Bstr(strOsTypeVBox).raw(), pGuestOSType.asOutParam());

            /* CPU count */
            ULONG cpuCountVBox;
            /* If there is a <vbox:Machine>, we always prefer the setting from there. */
            if (   vsysThis.pelmVBoxMachine
                && pNewDesc->m->pConfig->hardwareMachine.cCPUs)
                cpuCountVBox = pNewDesc->m->pConfig->hardwareMachine.cCPUs;
            else
                cpuCountVBox = vsysThis.cCPUs;
            /* Check for the constraints */
            if (cpuCountVBox > SchemaDefs::MaxCPUCount)
            {
                i_addWarning(tr("Virtual appliance \"%s\" was configured with %u CPUs however VirtualBox "
                                "supports a maximum of %u CPUs. Setting the CPU count to %u."),
                                vsysThis.strName.c_str(), cpuCountVBox, SchemaDefs::MaxCPUCount, SchemaDefs::MaxCPUCount);
                cpuCountVBox = SchemaDefs::MaxCPUCount;
            }
            if (vsysThis.cCPUs == 0)
                cpuCountVBox = 1;
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_CPU,
                                "",
                                Utf8StrFmt("%RU32", (uint32_t)vsysThis.cCPUs),
                                Utf8StrFmt("%RU32", (uint32_t)cpuCountVBox));

            /* RAM (in bytes) */
            uint64_t ullMemSizeVBox;
            /* If there is a <vbox:Machine>, we always prefer the setting from there. */
            if (   vsysThis.pelmVBoxMachine
                && pNewDesc->m->pConfig->hardwareMachine.ulMemorySizeMB)
                ullMemSizeVBox = (uint64_t)pNewDesc->m->pConfig->hardwareMachine.ulMemorySizeMB * _1M;
            else
                /* already in bytes via OVFReader::HandleVirtualSystemContent() */
                ullMemSizeVBox = vsysThis.ullMemorySize;
            /* Check for the constraints */
            if (    ullMemSizeVBox != 0
                 && (    ullMemSizeVBox < MM_RAM_MIN
                      || ullMemSizeVBox > MM_RAM_MAX
                    )
               )
            {
                i_addWarning(tr("Virtual appliance \"%s\" was configured with %RU64 MB of memory (RAM) "
                                "however VirtualBox supports a minimum of %u MB and a maximum of %u MB "
                                "of memory."),
                                vsysThis.strName.c_str(), ullMemSizeVBox / _1M, MM_RAM_MIN_IN_MB, MM_RAM_MAX_IN_MB);
                ullMemSizeVBox = RT_MIN(RT_MAX(ullMemSizeVBox, MM_RAM_MIN_IN_MB), MM_RAM_MAX_IN_MB);
            }
            if (vsysThis.ullMemorySize == 0)
            {
                /* If the RAM of the OVF is zero, use our predefined values */
                ULONG memSizeVBox2;
                if (!pGuestOSType.isNull())
                {
                    hrc = pGuestOSType->COMGETTER(RecommendedRAM)(&memSizeVBox2);
                    if (FAILED(hrc)) throw hrc;
                }
                else
                    memSizeVBox2 = 1024;
                /* IGuestOSType::recommendedRAM() returns the size in MB so convert to bytes */
                ullMemSizeVBox = (uint64_t)memSizeVBox2 * _1M;
            }
            /* It's alway stored in bytes in VSD according to the old internal agreement within the team */
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_Memory,
                                 "",
                                 Utf8StrFmt("%RU64", vsysThis.ullMemorySize),
                                 Utf8StrFmt("%RU64", ullMemSizeVBox));

            /* The NVRAM file does not have a <vbox:Machine> entry so we only need to check the OVF details. */
            if (vsysThis.strNvramPath.isNotEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_NVRAM, "", vsysThis.strNvramPath, vsysThis.strNvramPath);

            /* Audio */
            Utf8Str strSoundCard;
            Utf8Str strSoundCardOrig;
            /* If there is a <vbox:Machine>, we always prefer the setting from there. */
            if (   vsysThis.pelmVBoxMachine
                && pNewDesc->m->pConfig->hardwareMachine.audioAdapter.fEnabled)
            {
                strSoundCard = Utf8StrFmt("%RU32",
                                          (uint32_t)pNewDesc->m->pConfig->hardwareMachine.audioAdapter.controllerType);
            }
            else if (vsysThis.strSoundCardType.isNotEmpty())
            {
                /* Set the AC97 always for the simple OVF case.
                 * @todo: figure out the hardware which could be possible */
                strSoundCard = Utf8StrFmt("%RU32", (uint32_t)AudioControllerType_AC97);
                strSoundCardOrig = vsysThis.strSoundCardType;
            }
            if (strSoundCard.isNotEmpty())
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_SoundCard,
                                     "",
                                     strSoundCardOrig,
                                     strSoundCard);

#ifdef VBOX_WITH_USB
            /* USB Controller */
            /* If there is a <vbox:Machine>, we always prefer the setting from there. */
            if (   (   vsysThis.pelmVBoxMachine
                    && pNewDesc->m->pConfig->hardwareMachine.usbSettings.llUSBControllers.size() > 0)
                || vsysThis.fHasUsbController)
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_USBController, "", "", "");
#endif /* VBOX_WITH_USB */

            /* Network Controller */
            /* If there is a <vbox:Machine>, we always prefer the setting from there. */
            if (vsysThis.pelmVBoxMachine)
            {
                uint32_t maxNetworkAdapters =
                    PlatformProperties::s_getMaxNetworkAdapters(pNewDesc->m->pConfig->hardwareMachine.platformSettings.chipsetType);

                const settings::NetworkAdaptersList &llNetworkAdapters = pNewDesc->m->pConfig->hardwareMachine.llNetworkAdapters;
                /* Check for the constraints */
                if (llNetworkAdapters.size() > maxNetworkAdapters)
                    i_addWarning(tr("Virtual appliance \"%s\" was configured with %zu network adapters however "
                                    "VirtualBox supports a maximum of %u network adapters.", "", llNetworkAdapters.size()),
                                    vsysThis.strName.c_str(), llNetworkAdapters.size(), maxNetworkAdapters);
                /* Iterate through all network adapters. */
                settings::NetworkAdaptersList::const_iterator it1;
                size_t a = 0;
                for (it1 = llNetworkAdapters.begin();
                     it1 != llNetworkAdapters.end() && a < maxNetworkAdapters;
                     ++it1, ++a)
                {
                    if (it1->fEnabled)
                    {
                        Utf8Str strMode = convertNetworkAttachmentTypeToString(it1->mode);
                        pNewDesc->i_addEntry(VirtualSystemDescriptionType_NetworkAdapter,
                                             "", // ref
                                             strMode, // orig
                                             Utf8StrFmt("%RU32", (uint32_t)it1->type), // conf
                                             0,
                                             Utf8StrFmt("slot=%RU32;type=%s", it1->ulSlot, strMode.c_str())); // extra conf
                    }
                }
            }
            /* else we use the ovf configuration. */
            else if (vsysThis.llEthernetAdapters.size() >  0)
            {
                size_t cEthernetAdapters = vsysThis.llEthernetAdapters.size();
                uint32_t const maxNetworkAdapters = PlatformProperties::s_getMaxNetworkAdapters(ChipsetType_PIIX3); /** @todo BUGBUG x86 only for now. */

                /* Check for the constraints */
                if (cEthernetAdapters > maxNetworkAdapters)
                    i_addWarning(tr("Virtual appliance \"%s\" was configured with %zu network adapters however "
                                    "VirtualBox supports a maximum of %u network adapters.", "", cEthernetAdapters),
                                    vsysThis.strName.c_str(), cEthernetAdapters, maxNetworkAdapters);

                /* Get the default network adapter type for the selected guest OS */
                NetworkAdapterType_T defaultAdapterVBox = NetworkAdapterType_Am79C970A;
                if (!pGuestOSType.isNull())
                {
                    hrc = pGuestOSType->COMGETTER(AdapterType)(&defaultAdapterVBox);
                    if (FAILED(hrc)) throw hrc;
                }
                else
                {
#ifdef VBOX_WITH_E1000
                    defaultAdapterVBox = NetworkAdapterType_I82540EM;
#else
                    defaultAdapterVBox = NetworkAdapterType_Am79C973A;
#endif
                }

                ovf::EthernetAdaptersList::const_iterator itEA;
                /* Iterate through all abstract networks. Ignore network cards
                 * which exceed the limit of VirtualBox. */
                size_t a = 0;
                for (itEA = vsysThis.llEthernetAdapters.begin();
                     itEA != vsysThis.llEthernetAdapters.end() && a < maxNetworkAdapters;
                     ++itEA, ++a)
                {
                    const ovf::EthernetAdapter &ea = *itEA; // logical network to connect to
                    Utf8Str strNetwork = ea.strNetworkName;
                    // make sure it's one of these two
                    if (    (strNetwork.compare("Null", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("NAT", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("Bridged", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("Internal", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("HostOnly", Utf8Str::CaseInsensitive))
                         && (strNetwork.compare("Generic", Utf8Str::CaseInsensitive))
                       )
                        strNetwork = "Bridged";     // VMware assumes this is the default apparently

                    /* Figure out the hardware type */
                    NetworkAdapterType_T nwAdapterVBox = defaultAdapterVBox;
                    if (!ea.strAdapterType.compare("PCNet32", Utf8Str::CaseInsensitive))
                    {
                        /* If the default adapter is already one of the two
                         * PCNet adapters use the default one. If not use the
                         * Am79C970A as fallback. */
                        if (!(defaultAdapterVBox == NetworkAdapterType_Am79C970A ||
                              defaultAdapterVBox == NetworkAdapterType_Am79C973))
                            nwAdapterVBox = NetworkAdapterType_Am79C970A;
                    }
#ifdef VBOX_WITH_E1000
                    /* VMWare accidentally write this with VirtualCenter 3.5,
                       so make sure in this case always to use the VMWare one */
                    else if (!ea.strAdapterType.compare("E10000", Utf8Str::CaseInsensitive))
                        nwAdapterVBox = NetworkAdapterType_I82545EM;
                    else if (!ea.strAdapterType.compare("E1000", Utf8Str::CaseInsensitive))
                    {
                        /* Check if this OVF was written by VirtualBox */
                        if (Utf8Str(vsysThis.strVirtualSystemType).contains("virtualbox", Utf8Str::CaseInsensitive))
                        {
                            /* If the default adapter is already one of the three
                             * E1000 adapters use the default one. If not use the
                             * I82545EM as fallback. */
                            if (!(defaultAdapterVBox == NetworkAdapterType_I82540EM ||
                                  defaultAdapterVBox == NetworkAdapterType_I82543GC ||
                                  defaultAdapterVBox == NetworkAdapterType_I82545EM))
                            nwAdapterVBox = NetworkAdapterType_I82540EM;
                        }
                        else
                            /* Always use this one since it's what VMware uses */
                            nwAdapterVBox = NetworkAdapterType_I82545EM;
                    }
#endif /* VBOX_WITH_E1000 */
                    else if (   !ea.strAdapterType.compare("VirtioNet", Utf8Str::CaseInsensitive)
                             || !ea.strAdapterType.compare("virtio-net", Utf8Str::CaseInsensitive)
                             || !ea.strAdapterType.compare("3", Utf8Str::CaseInsensitive))
                        nwAdapterVBox = NetworkAdapterType_Virtio;

                    pNewDesc->i_addEntry(VirtualSystemDescriptionType_NetworkAdapter,
                                         "",      // ref
                                         ea.strNetworkName,      // orig
                                         Utf8StrFmt("%RU32", (uint32_t)nwAdapterVBox),   // conf
                                         0,
                                         Utf8StrFmt("type=%s", strNetwork.c_str()));       // extra conf
                }
            }

            /* If there is a <vbox:Machine>, we always prefer the setting from there. */
            bool fFloppy = false;
            bool fDVD = false;
            if (vsysThis.pelmVBoxMachine)
            {
                settings::StorageControllersList &llControllers = pNewDesc->m->pConfig->hardwareMachine.storage.llStorageControllers;
                settings::StorageControllersList::iterator it3;
                for (it3 = llControllers.begin();
                     it3 != llControllers.end();
                     ++it3)
                {
                    settings::AttachedDevicesList &llAttachments = it3->llAttachedDevices;
                    settings::AttachedDevicesList::iterator it4;
                    for (it4 = llAttachments.begin();
                         it4 != llAttachments.end();
                         ++it4)
                    {
                        fDVD |= it4->deviceType == DeviceType_DVD;
                        fFloppy |= it4->deviceType == DeviceType_Floppy;
                        if (fFloppy && fDVD)
                            break;
                    }
                    if (fFloppy && fDVD)
                        break;
                }
            }
            else
            {
                fFloppy = vsysThis.fHasFloppyDrive;
                fDVD = vsysThis.fHasCdromDrive;
            }
            /* Floppy Drive */
            if (fFloppy)
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_Floppy, "", "", "");
            /* CD Drive */
            if (fDVD)
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_CDROM, "", "", "");

            /* Storage Controller */
            uint16_t cIDEused = 0;
            uint16_t cSATAused = 0; NOREF(cSATAused);
            uint16_t cSCSIused = 0; NOREF(cSCSIused);
            uint16_t cVIRTIOSCSIused = 0; NOREF(cVIRTIOSCSIused);
            uint16_t cNVMeused = 0; NOREF(cNVMeused);

            ovf::ControllersMap::const_iterator hdcIt;
            /* Iterate through all storage controllers */
            for (hdcIt = vsysThis.mapControllers.begin();
                 hdcIt != vsysThis.mapControllers.end();
                 ++hdcIt)
            {
                const ovf::HardDiskController &hdc = hdcIt->second;

                /** @todo r=andy The following count checks look messy/wrong compared to what we define
                 *               elsewhere (e.g. PlatformProperties).  Needs to be checked (and fixed). */

                switch (hdc.system)
                {
                    case ovf::HardDiskController::IDE:
                        /* Check for the constraints */
                        if (cIDEused < 4) /** @todo r=andy Is this really true? */
                        {
                            /// @todo figure out the IDE types
                            /* Use PIIX4 as default */
                            Utf8Str strType = "PIIX4";
                            if (!hdc.strControllerType.compare("PIIX3", Utf8Str::CaseInsensitive))
                                strType = "PIIX3";
                            else if (!hdc.strControllerType.compare("ICH6", Utf8Str::CaseInsensitive))
                                strType = "ICH6";
                            pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerIDE,
                                                 hdc.strIdController,       // strRef
                                                 hdc.strControllerType,     // aOvfValue
                                                 strType);                  // aVBoxValue
                        }
                        else
                            /* Warn only once */
                            if (cIDEused == 4) /** @todo r=andy See todo above. */
                                i_addWarning(tr("Virtual appliance \"%s\" was configured with more than two "
                                                "IDE controllers however VirtualBox supports a maximum of two "
                                                "IDE controllers."),
                                                vsysThis.strName.c_str());

                        ++cIDEused;
                    break;

                    case ovf::HardDiskController::SATA:
                        /* Check for the constraints */
                        if (cSATAused < 1)
                        {
                            /// @todo figure out the SATA types
                            /* We only support a plain AHCI controller, so use them always */
                            pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerSATA,
                                                 hdc.strIdController,
                                                 hdc.strControllerType,
                                                 "AHCI");
                        }
                        else
                        {
                            /* Warn only once */
                            if (cSATAused == 1)
                                i_addWarning(tr("Virtual appliance \"%s\" was configured with more than one "
                                                "SATA controller however VirtualBox supports a maximum of one "
                                                "SATA controller."),
                                                vsysThis.strName.c_str());

                        }
                        ++cSATAused;
                    break;

                    case ovf::HardDiskController::SCSI:
                        /* Check for the constraints */
                        if (cSCSIused < 1)
                        {
                            VirtualSystemDescriptionType_T vsdet = VirtualSystemDescriptionType_HardDiskControllerSCSI;
                            Utf8Str hdcController = "LsiLogic";
                            if (!hdc.strControllerType.compare("lsilogicsas", Utf8Str::CaseInsensitive))
                            {
                                // OVF considers SAS a variant of SCSI but VirtualBox considers it a class of its own
                                vsdet = VirtualSystemDescriptionType_HardDiskControllerSAS;
                                hdcController = "LsiLogicSas";
                            }
                            else if (!hdc.strControllerType.compare("BusLogic", Utf8Str::CaseInsensitive))
                                hdcController = "BusLogic";
                            pNewDesc->i_addEntry(vsdet,
                                                 hdc.strIdController,
                                                 hdc.strControllerType,
                                                 hdcController);
                        }
                        else
                            i_addWarning(tr("Virtual appliance \"%s\" was configured with more than one SCSI "
                                            "controller of type \"%s\" with ID %s however VirtualBox supports "
                                            "a maximum of one SCSI controller for each type."),
                                            vsysThis.strName.c_str(),
                                            hdc.strControllerType.c_str(),
                                            hdc.strIdController.c_str());
                        ++cSCSIused;
                    break;

                    case ovf::HardDiskController::VIRTIOSCSI:
                        /* Check for the constraints */
                        if (cVIRTIOSCSIused < 1)
                        {
                            pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI,
                                                 hdc.strIdController,
                                                 hdc.strControllerType,
                                                 "VirtioSCSI");
                        }
                        else
                        {
                            /* Warn only once */
                            if (cVIRTIOSCSIused == 1)
                                i_addWarning(tr("Virtual appliance \"%s\" was configured with more than one "
                                                "VirtioSCSI controller however VirtualBox supports a maximum "
                                                "of one VirtioSCSI controller."),
                                                vsysThis.strName.c_str());

                        }
                        ++cVIRTIOSCSIused;
                    break;

                    case ovf::HardDiskController::NVMe:
                        /* Check for the constraints */
                        if (cNVMeused < 1)
                        {
                            pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerNVMe,
                                                 hdc.strIdController,
                                                 hdc.strControllerType,
                                                 "NVMe");
                        }
                        else
                        {
                            /* Warn only once */
                            if (cNVMeused == 1)
                                i_addWarning(tr("Virtual appliance \"%s\" was configured with more than one "
                                                "NVMe controller however VirtualBox supports a maximum "
                                                "of one NVMe controller."),
                                                vsysThis.strName.c_str());

                        }
                        ++cNVMeused;
                    break;

                }
            }

            /* Storage devices (hard disks/DVDs/...) */
            if (vsysThis.mapVirtualDisks.size() > 0)
            {
                ovf::VirtualDisksMap::const_iterator itVD;
                /* Iterate through all storage devices */
                for (itVD = vsysThis.mapVirtualDisks.begin();
                     itVD != vsysThis.mapVirtualDisks.end();
                     ++itVD)
                {
                    const ovf::VirtualDisk &hd = itVD->second;
                    /* Get the associated image */
                    ovf::DiskImage di;
                    std::map<RTCString, ovf::DiskImage>::iterator foundDisk;

                    foundDisk = m->pReader->m_mapDisks.find(hd.strDiskId);
                    if (foundDisk == m->pReader->m_mapDisks.end())
                        continue;
                    else
                    {
                        di = foundDisk->second;
                    }

                    /*
                     * Figure out from URI which format the image has.
                     * There is no strict mapping of image URI to image format.
                     * It's possible we aren't able to recognize some URIs.
                     */

                    ComObjPtr<MediumFormat> mediumFormat;
                    hrc = i_findMediumFormatFromDiskImage(di, mediumFormat);
                    if (FAILED(hrc))
                        throw hrc;

                    Bstr bstrFormatName;
                    hrc = mediumFormat->COMGETTER(Name)(bstrFormatName.asOutParam());
                    if (FAILED(hrc))
                        throw hrc;
                    Utf8Str vdf = Utf8Str(bstrFormatName);

                    /// @todo
                    //  - figure out all possible vmdk formats we also support
                    //  - figure out if there is a url specifier for vhd already
                    //  - we need a url specifier for the vdi format

                    Utf8Str strFilename = di.strHref;
                    DeviceType_T devType = DeviceType_Null;
                    if (vdf.compare("VMDK", Utf8Str::CaseInsensitive) == 0)
                    {
                        /* If the href is empty use the VM name as filename */
                        if (!strFilename.length())
                            strFilename = Utf8StrFmt("%s.vmdk", hd.strDiskId.c_str());
                        devType = DeviceType_HardDisk;
                    }
                    else if (vdf.compare("RAW", Utf8Str::CaseInsensitive) == 0)
                    {
                        /* If the href is empty use the VM name as filename */
                        if (!strFilename.length())
                            strFilename = Utf8StrFmt("%s.iso", hd.strDiskId.c_str());
                        devType = DeviceType_DVD;
                    }
                    else
                        throw setError(VBOX_E_FILE_ERROR,
                                       tr("Unsupported format for virtual disk image %s in OVF: \"%s\""),
                                          di.strHref.c_str(),
                                          di.strFormat.c_str());

                    /*
                     * Remove last extension from the file name if the file is compressed
                     */
                    if (di.strCompression.compare("gzip", Utf8Str::CaseInsensitive)==0)
                        strFilename.stripSuffix();

                    i_ensureUniqueImageFilePath(strMachineFolder, devType, strFilename); /** @todo check the return code! */

                    /* find the description for the storage controller
                     * that has the same ID as hd.strIdController */
                    const VirtualSystemDescriptionEntry *pController;
                    if (!(pController = pNewDesc->i_findControllerFromID(hd.strIdController)))
                        throw setError(E_FAIL,
                                       tr("Cannot find storage controller with OVF instance ID \"%s\" "
                                          "to which medium \"%s\" should be attached"),
                                       hd.strIdController.c_str(),
                                       di.strHref.c_str());

                    /* controller to attach to, and the bus within that controller */
                    Utf8StrFmt strExtraConfig("controller=%RI16;channel=%RI16",
                                              pController->ulIndex,
                                              hd.ulAddressOnParent);
                    pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskImage,
                                         hd.strDiskId,
                                         di.strHref,
                                         strFilename,
                                         di.ulSuggestedSizeMB,
                                         strExtraConfig);
                }
            }

            m->virtualSystemDescriptions.push_back(pNewDesc);
        }
    }
    catch (HRESULT hrcXcpt)
    {
        /* On error we clear the list & return */
        m->virtualSystemDescriptions.clear();
        hrc = hrcXcpt;
    }

    // reset the appliance state
    alock.acquire();
    m->state = ApplianceIdle;

    return hrc;
}

/**
 * Public method implementation. This creates one or more new machines according to the
 * VirtualSystemScription instances created by Appliance::Interpret().
 * Thread implementation is in Appliance::i_importImpl().
 * @param aOptions  Import options.
 * @param aProgress Progress object.
 * @return
 */
HRESULT Appliance::importMachines(const std::vector<ImportOptions_T> &aOptions,
                                  ComPtr<IProgress> &aProgress)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aOptions.size())
    {
        try
        {
            m->optListImport.setCapacity(aOptions.size());
            for (size_t i = 0; i < aOptions.size(); ++i)
                m->optListImport.insert(i, aOptions[i]);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }

    AssertReturn(!(   m->optListImport.contains(ImportOptions_KeepAllMACs)
                   && m->optListImport.contains(ImportOptions_KeepNATMACs) )
                  , E_INVALIDARG);

    // do not allow entering this method if the appliance is busy reading or writing
    if (!i_isApplianceIdle())
        return E_ACCESSDENIED;

    //check for the local import only. For import from the Cloud m->pReader is always NULL.
    if (m->locInfo.storageType == VFSType_File && !m->pReader)
        return setError(E_FAIL,
                        tr("Cannot import machines without reading it first (call read() before i_importMachines())"));

    ComObjPtr<Progress> progress;
    HRESULT hrc = i_importImpl(m->locInfo, progress);
    if (SUCCEEDED(hrc))
        progress.queryInterfaceTo(aProgress.asOutParam());

    return hrc;
}

////////////////////////////////////////////////////////////////////////////////
//
// Appliance private methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Ensures that there is a look-ahead object ready.
 *
 * @returns true if there's an object handy, false if end-of-stream.
 * @throws HRESULT if the next object isn't a regular file. Sets error info
 *                 (which is why it's a method on Appliance and not the
 *                 ImportStack).
 */
bool Appliance::i_importEnsureOvaLookAhead(ImportStack &stack)
{
    Assert(stack.hVfsFssOva != NULL);
    if (stack.hVfsIosOvaLookAhead == NIL_RTVFSIOSTREAM)
    {
        RTStrFree(stack.pszOvaLookAheadName);
        stack.pszOvaLookAheadName = NULL;

        RTVFSOBJTYPE enmType;
        RTVFSOBJ hVfsObj;
        int vrc = RTVfsFsStrmNext(stack.hVfsFssOva, &stack.pszOvaLookAheadName, &enmType, &hVfsObj);
        if (RT_SUCCESS(vrc))
        {
            stack.hVfsIosOvaLookAhead = RTVfsObjToIoStream(hVfsObj);
            RTVfsObjRelease(hVfsObj);
            if (   (   enmType != RTVFSOBJTYPE_FILE
                    && enmType != RTVFSOBJTYPE_IO_STREAM)
                || stack.hVfsIosOvaLookAhead == NIL_RTVFSIOSTREAM)
                throw setError(VBOX_E_FILE_ERROR,
                               tr("Malformed OVA. '%s' is not a regular file (%d)."), stack.pszOvaLookAheadName, enmType);
        }
        else if (vrc == VERR_EOF)
            return false;
        else
            throw setErrorVrc(vrc, tr("RTVfsFsStrmNext failed (%Rrc)"), vrc);
    }
    return true;
}

HRESULT Appliance::i_preCheckImageAvailability(ImportStack &stack)
{
    if (i_importEnsureOvaLookAhead(stack))
        return S_OK;
    throw setError(VBOX_E_FILE_ERROR, tr("Unexpected end of OVA package"));
    /** @todo r=bird: dunno why this bother returning a value and the caller
     *        having a special 'continue' case for it. It always threw all non-OK
     *        status codes.  It's possibly to handle out of order stuff, so that
     *        needs adding to the testcase! */
}

/**
 * Opens a source file (for reading obviously).
 *
 * @param   stack
 * @param   rstrSrcPath         The source file to open.
 * @param   pszManifestEntry    The manifest entry of the source file.  This is
 *                              used when constructing our manifest using a pass
 *                              thru.
 * @returns I/O stream handle to the source file.
 * @throws  HRESULT error status, error info set.
 */
RTVFSIOSTREAM Appliance::i_importOpenSourceFile(ImportStack &stack, Utf8Str const &rstrSrcPath, const char *pszManifestEntry)
{
    /*
     * Open the source file.  Special considerations for OVAs.
     */
    RTVFSIOSTREAM hVfsIosSrc;
    if (stack.hVfsFssOva != NIL_RTVFSFSSTREAM)
    {
        for (uint32_t i = 0;; i++)
        {
            if (!i_importEnsureOvaLookAhead(stack))
                throw setErrorBoth(VBOX_E_FILE_ERROR, VERR_EOF,
                                   tr("Unexpected end of OVA / internal error - missing '%s' (skipped %u)"),
                                   rstrSrcPath.c_str(), i);
            if (RTStrICmp(stack.pszOvaLookAheadName, rstrSrcPath.c_str()) == 0)
                break;

            /* release the current object, loop to get the next. */
            RTVfsIoStrmRelease(stack.claimOvaLookAHead());
        }
        hVfsIosSrc = stack.claimOvaLookAHead();
    }
    else
    {
        int vrc = RTVfsIoStrmOpenNormal(rstrSrcPath.c_str(), RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE, &hVfsIosSrc);
        if (RT_FAILURE(vrc))
            throw setErrorVrc(vrc, tr("Error opening '%s' for reading (%Rrc)"), rstrSrcPath.c_str(), vrc);
    }

    /*
     * Digest calculation filtering.
     */
    hVfsIosSrc = i_manifestSetupDigestCalculationForGivenIoStream(hVfsIosSrc, pszManifestEntry);
    if (hVfsIosSrc == NIL_RTVFSIOSTREAM)
        throw E_FAIL;

    return hVfsIosSrc;
}

/**
 * Creates the destination file and fills it with bytes from the source stream.
 *
 * This assumes that we digest the source when fDigestTypes is non-zero, and
 * thus calls RTManifestPtIosAddEntryNow when done.
 *
 * @param   rstrDstPath     The path to the destination file.  Missing path
 *                          components will be created.
 * @param   hVfsIosSrc      The source I/O stream.
 * @param   rstrSrcLogNm    The name of the source for logging and error
 *                          messages.
 * @returns COM status code.
 * @throws Nothing (as the caller has VFS handles to release).
 */
HRESULT Appliance::i_importCreateAndWriteDestinationFile(Utf8Str const &rstrDstPath, RTVFSIOSTREAM hVfsIosSrc,
                                                         Utf8Str const &rstrSrcLogNm)
{
    int vrc;

    /*
     * Create the output file, including necessary paths.
     * Any existing file will be overwritten.
     */
    HRESULT hrc = VirtualBox::i_ensureFilePathExists(rstrDstPath, true /*fCreate*/);
    if (SUCCEEDED(hrc))
    {
        RTVFSIOSTREAM hVfsIosDst;
        vrc = RTVfsIoStrmOpenNormal(rstrDstPath.c_str(),
                                    RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_ALL,
                                    &hVfsIosDst);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Pump the bytes thru. If we fail, delete the output file.
             */
            vrc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, 0);
            if (RT_SUCCESS(vrc))
                hrc = S_OK;
            else
                hrc = setErrorVrc(vrc, tr("Error occured decompressing '%s' to '%s' (%Rrc)"),
                                  rstrSrcLogNm.c_str(), rstrDstPath.c_str(), vrc);
            uint32_t cRefs = RTVfsIoStrmRelease(hVfsIosDst);
            AssertMsg(cRefs == 0, ("cRefs=%u\n", cRefs)); NOREF(cRefs);
            if (RT_FAILURE(vrc))
                RTFileDelete(rstrDstPath.c_str());
        }
        else
            hrc = setErrorVrc(vrc, tr("Error opening destionation image '%s' for writing (%Rrc)"), rstrDstPath.c_str(), vrc);
    }
    return hrc;
}


/**
 *
 * @param   stack               Import stack.
 * @param   rstrSrcPath         Source path.
 * @param   rstrDstPath         Destination path.
 * @param   pszManifestEntry    The manifest entry of the source file.  This is
 *                              used when constructing our manifest using a pass
 *                              thru.
 * @throws HRESULT error status, error info set.
 */
void Appliance::i_importCopyFile(ImportStack &stack, Utf8Str const &rstrSrcPath, Utf8Str const &rstrDstPath,
                                 const char *pszManifestEntry)
{
    /*
     * Open the file (throws error) and add a read ahead thread so we can do
     * concurrent reads (+digest) and writes.
     */
    RTVFSIOSTREAM hVfsIosSrc = i_importOpenSourceFile(stack, rstrSrcPath, pszManifestEntry);
    RTVFSIOSTREAM hVfsIosReadAhead;
    int vrc = RTVfsCreateReadAheadForIoStream(hVfsIosSrc, 0 /*fFlags*/, 0 /*cBuffers=default*/, 0 /*cbBuffers=default*/,
                                              &hVfsIosReadAhead);
    if (RT_FAILURE(vrc))
    {
        RTVfsIoStrmRelease(hVfsIosSrc);
        throw setErrorVrc(vrc, tr("Error initializing read ahead thread for '%s' (%Rrc)"), rstrSrcPath.c_str(), vrc);
    }

    /*
     * Write the destination file (nothrow).
     */
    HRESULT hrc = i_importCreateAndWriteDestinationFile(rstrDstPath, hVfsIosReadAhead, rstrSrcPath);
    RTVfsIoStrmRelease(hVfsIosReadAhead);

    /*
     * Before releasing the source stream, make sure we've successfully added
     * the digest to our manifest.
     */
    if (SUCCEEDED(hrc) && m->fDigestTypes)
    {
        vrc = RTManifestPtIosAddEntryNow(hVfsIosSrc);
        if (RT_FAILURE(vrc))
            hrc = setErrorVrc(vrc, tr("RTManifestPtIosAddEntryNow failed with %Rrc"), vrc);
    }

    uint32_t cRefs = RTVfsIoStrmRelease(hVfsIosSrc);
    AssertMsg(cRefs == 0, ("cRefs=%u\n", cRefs)); NOREF(cRefs);
    if (SUCCEEDED(hrc))
        return;
    throw hrc;
}

/**
 *
 * @param   stack
 * @param   rstrSrcPath
 * @param   rstrDstPath
 * @param   pszManifestEntry    The manifest entry of the source file.  This is
 *                              used when constructing our manifest using a pass
 *                              thru.
 * @throws HRESULT error status, error info set.
 */
void Appliance::i_importDecompressFile(ImportStack &stack, Utf8Str const &rstrSrcPath, Utf8Str const &rstrDstPath,
                                       const char *pszManifestEntry)
{
    RTVFSIOSTREAM hVfsIosSrcCompressed = i_importOpenSourceFile(stack, rstrSrcPath, pszManifestEntry);

    /*
     * Add a read ahead thread here.  This means reading and digest calculation
     * is done on one thread, while unpacking and writing is one on this thread.
     */
    RTVFSIOSTREAM hVfsIosReadAhead;
    int vrc = RTVfsCreateReadAheadForIoStream(hVfsIosSrcCompressed, 0 /*fFlags*/, 0 /*cBuffers=default*/,
                                              0 /*cbBuffers=default*/, &hVfsIosReadAhead);
    if (RT_FAILURE(vrc))
    {
        RTVfsIoStrmRelease(hVfsIosSrcCompressed);
        throw setErrorVrc(vrc, tr("Error initializing read ahead thread for '%s' (%Rrc)"), rstrSrcPath.c_str(), vrc);
    }

    /*
     * Add decompression step.
     */
    RTVFSIOSTREAM hVfsIosSrc;
    vrc = RTZipGzipDecompressIoStream(hVfsIosReadAhead, 0, &hVfsIosSrc);
    RTVfsIoStrmRelease(hVfsIosReadAhead);
    if (RT_FAILURE(vrc))
    {
        RTVfsIoStrmRelease(hVfsIosSrcCompressed);
        throw setErrorVrc(vrc, tr("Error initializing gzip decompression for '%s' (%Rrc)"), rstrSrcPath.c_str(), vrc);
    }

    /*
     * Write the stream to the destination file (nothrow).
     */
    HRESULT hrc = i_importCreateAndWriteDestinationFile(rstrDstPath, hVfsIosSrc, rstrSrcPath);

    /*
     * Before releasing the source stream, make sure we've successfully added
     * the digest to our manifest.
     */
    if (SUCCEEDED(hrc) && m->fDigestTypes)
    {
        vrc = RTManifestPtIosAddEntryNow(hVfsIosSrcCompressed);
        if (RT_FAILURE(vrc))
            hrc = setErrorVrc(vrc, tr("RTManifestPtIosAddEntryNow failed with %Rrc"), vrc);
    }

    uint32_t cRefs = RTVfsIoStrmRelease(hVfsIosSrc);
    AssertMsg(cRefs == 0, ("cRefs=%u\n", cRefs)); NOREF(cRefs);

    cRefs = RTVfsIoStrmRelease(hVfsIosSrcCompressed);
    AssertMsg(cRefs == 0, ("cRefs=%u\n", cRefs)); NOREF(cRefs);

    if (SUCCEEDED(hrc))
        return;
    throw hrc;
}

/*******************************************************************************
 * Read stuff
 ******************************************************************************/

/**
 * Implementation for reading an OVF (via task).
 *
 * This starts a new thread which will call
 * Appliance::taskThreadImportOrExport() which will then call readFS(). This
 * will then open the OVF with ovfreader.cpp.
 *
 * This is in a separate private method because it is used from two locations:
 *
 * 1) from the public Appliance::Read().
 *
 * 2) in a second worker thread; in that case, Appliance::ImportMachines() called Appliance::i_importImpl(), which
 *    called Appliance::readFSOVA(), which called Appliance::i_importImpl(), which then called this again.
 *
 * @returns COM status with error info set.
 * @param   aLocInfo    The OVF location.
 * @param   aProgress   Where to return the progress object.
 */
HRESULT Appliance::i_readImpl(const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress)
{
    /*
     * Create the progress object.
     */
    HRESULT hrc;
    aProgress.createObject();
    try
    {
        if (aLocInfo.storageType == VFSType_Cloud)
        {
            /* 1 operation only */
            hrc = aProgress->init(mVirtualBox, static_cast<IAppliance*>(this),
                                  Utf8Str(tr("Getting cloud instance information")), TRUE /* aCancelable */);

            /* Create an empty ovf::OVFReader for manual filling it.
             * It's not a normal usage case, but we try to re-use some OVF stuff to friend
             * the cloud import with OVF import.
             * In the standard case the ovf::OVFReader is created in the Appliance::i_readOVFFile().
             * We need the existing m->pReader for Appliance::i_importCloudImpl() where we re-use OVF logic. */
            m->pReader = new ovf::OVFReader();
        }
        else
        {
            Utf8StrFmt strDesc(tr("Reading appliance '%s'"), aLocInfo.strPath.c_str());
            if (aLocInfo.storageType == VFSType_File)
                /* 1 operation only */
                hrc = aProgress->init(mVirtualBox, static_cast<IAppliance*>(this), strDesc, TRUE /* aCancelable */);
            else
                /* 4/5 is downloading, 1/5 is reading */
                hrc = aProgress->init(mVirtualBox, static_cast<IAppliance*>(this), strDesc, TRUE /* aCancelable */,
                                      2, // ULONG cOperations,
                                      5, // ULONG ulTotalOperationsWeight,
                                      Utf8StrFmt(tr("Download appliance '%s'"),
                                                 aLocInfo.strPath.c_str()), // CBSTR bstrFirstOperationDescription,
                                      4); // ULONG ulFirstOperationWeight,
        }
    }
    catch (std::bad_alloc &) /* Utf8Str/Utf8StrFmt */
    {
        return E_OUTOFMEMORY;
    }
    if (FAILED(hrc))
        return hrc;

    /*
     * Initialize the worker task.
     */
    ThreadTask *pTask;
    try
    {
        if (aLocInfo.storageType == VFSType_Cloud)
            pTask = new TaskCloud(this, TaskCloud::ReadData, aLocInfo, aProgress);
        else
            pTask = new TaskOVF(this, TaskOVF::Read, aLocInfo, aProgress);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Kick off the worker thread.
     */
    hrc = pTask->createThread();
    pTask = NULL; /* Note! createThread has consumed the task.*/
    if (SUCCEEDED(hrc))
        return hrc;
    return setError(hrc, tr("Failed to create thread for reading appliance data"));
}

HRESULT Appliance::i_gettingCloudData(TaskCloud *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    try
    {
        Utf8Str strBasename(pTask->locInfo.strPath);
        RTCList<RTCString, RTCString *> parts = strBasename.split("/");
        if (parts.size() != 2)//profile + instance id
            return setErrorVrc(VERR_MISMATCH,
                               tr("%s: The profile name or instance id are absent or contain unsupported characters: %s"),
                               __FUNCTION__, strBasename.c_str());

        //Get information about the passed cloud instance
        ComPtr<ICloudProviderManager> cpm;
        hrc = mVirtualBox->COMGETTER(CloudProviderManager)(cpm.asOutParam());
        if (FAILED(hrc))
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud provider manager object wasn't found (%Rhrc)"), __FUNCTION__, hrc);

        Utf8Str strProviderName = pTask->locInfo.strProvider;
        ComPtr<ICloudProvider> cloudProvider;
        ComPtr<ICloudProfile> cloudProfile;
        hrc = cpm->GetProviderByShortName(Bstr(strProviderName.c_str()).raw(), cloudProvider.asOutParam());

        if (FAILED(hrc))
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud provider object wasn't found (%Rhrc)"), __FUNCTION__, hrc);

        Utf8Str profileName(parts.at(0));//profile
        if (profileName.isEmpty())
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud user profile name wasn't found (%Rhrc)"), __FUNCTION__, hrc);

        hrc = cloudProvider->GetProfileByName(Bstr(parts.at(0)).raw(), cloudProfile.asOutParam());
        if (FAILED(hrc))
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud profile object wasn't found (%Rhrc)"), __FUNCTION__, hrc);

        ComObjPtr<ICloudClient> cloudClient;
        hrc = cloudProfile->CreateCloudClient(cloudClient.asOutParam());
        if (FAILED(hrc))
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud client object wasn't found (%Rhrc)"), __FUNCTION__, hrc);

        m->virtualSystemDescriptions.clear();//clear all for assurance before creating new
        std::vector<ComPtr<IVirtualSystemDescription> > vsdArray;
        ULONG requestedVSDnums = 1;
        ULONG newVSDnums = 0;
        hrc = createVirtualSystemDescriptions(requestedVSDnums, &newVSDnums);
        if (FAILED(hrc)) throw hrc;
        if (requestedVSDnums != newVSDnums)
            throw setErrorVrc(VERR_MISMATCH, tr("%s: Requested (%d) and created (%d) numbers of VSD are differ ."),
                              __FUNCTION__, requestedVSDnums, newVSDnums);

        hrc = getVirtualSystemDescriptions(vsdArray);
        if (FAILED(hrc)) throw hrc;
        ComPtr<IVirtualSystemDescription> instanceDescription = vsdArray[0];

        LogRel(("%s: calling CloudClient::GetInstanceInfo()\n", __FUNCTION__));

        ComPtr<IProgress> pProgress;
        hrc = cloudClient->GetInstanceInfo(Bstr(parts.at(1)).raw(), instanceDescription, pProgress.asOutParam());
        if (FAILED(hrc)) throw hrc;
        hrc = pTask->pProgress->WaitForOtherProgressCompletion(pProgress, 60000);//timeout 1 min = 60000 millisec
        if (FAILED(hrc)) throw hrc;

        // set cloud profile
        instanceDescription->AddDescription(VirtualSystemDescriptionType_CloudProfileName, Bstr(profileName).raw(),  NULL);

        Utf8StrFmt strSetting("VM with id %s imported from the cloud provider %s",
                              parts.at(1).c_str(), strProviderName.c_str());
        // set description
        instanceDescription->AddDescription(VirtualSystemDescriptionType_Description, Bstr(strSetting).raw(), NULL);
    }
    catch (HRESULT arc)
    {
        LogFlowFunc(("arc=%Rhrc\n", arc));
        hrc = arc;
    }

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    LogFlowFuncLeave();

    return hrc;
}

void Appliance::i_setApplianceState(const ApplianceState &state)
{
    AutoWriteLock writeLock(this COMMA_LOCKVAL_SRC_POS);
    m->state = state;
    writeLock.release();
}

/**
 * Actual worker code for import from the Cloud
 *
 * @param pTask
 * @return
 */
HRESULT Appliance::i_importCloudImpl(TaskCloud *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    int vrc = VINF_SUCCESS;
    /** @todo r=klaus This should be a MultiResult, because this can cause
     * multiple errors and warnings which should be relevant for the caller.
     * Needs some work, because there might be errors which need to be
     * excluded if they happen in error recovery code paths. */
    HRESULT hrc = S_OK;
    bool fKeepDownloadedObject = false;//in the future should be passed from the caller

    /* Clear the list of imported machines, if any */
    m->llGuidsMachinesCreated.clear();

    ComPtr<ICloudProviderManager> cpm;
    hrc = mVirtualBox->COMGETTER(CloudProviderManager)(cpm.asOutParam());
    if (FAILED(hrc))
        return setErrorVrc(VERR_COM_OBJECT_NOT_FOUND, tr("%s: Cloud provider manager object wasn't found"), __FUNCTION__);

    Utf8Str strProviderName = pTask->locInfo.strProvider;
    ComPtr<ICloudProvider> cloudProvider;
    ComPtr<ICloudProfile> cloudProfile;
    hrc = cpm->GetProviderByShortName(Bstr(strProviderName.c_str()).raw(), cloudProvider.asOutParam());

    if (FAILED(hrc))
        return setErrorVrc(VERR_COM_OBJECT_NOT_FOUND, tr("%s: Cloud provider object wasn't found"), __FUNCTION__);

    /* Get the actual VSD, only one VSD object can be there for now so just call the function front() */
    ComPtr<IVirtualSystemDescription> vsd = m->virtualSystemDescriptions.front();

    Utf8Str vsdData;
    com::SafeArray<VirtualSystemDescriptionType_T> retTypes;
    com::SafeArray<BSTR> aRefs;
    com::SafeArray<BSTR> aOvfValues;
    com::SafeArray<BSTR> aVBoxValues;
    com::SafeArray<BSTR> aExtraConfigValues;

/*
 * local #define  for better reading the code
 * uses only the previously locally declared variable names
 * set hrc as the result of operation
 *
 * What the above description fail to say is that this returns:
 *      - retTypes
 *      - aRefs
 *      - aOvfValues
 *      - aVBoxValues
 *      - aExtraConfigValues
 */
/** @todo r=bird: The setNull calls here are implicit in ComSafeArraySasOutParam,
 * so we're doing twice here for no good reason!  Btw. very untidy to not wrap
 * this in do { } while (0) and require ';' when used.  */
#define GET_VSD_DESCRIPTION_BY_TYPE(aParamType) do { \
        retTypes.setNull(); \
        aRefs.setNull(); \
        aOvfValues.setNull(); \
        aVBoxValues.setNull(); \
        aExtraConfigValues.setNull(); \
        vsd->GetDescriptionByType(aParamType, \
                                  ComSafeArrayAsOutParam(retTypes), \
                                  ComSafeArrayAsOutParam(aRefs), \
                                  ComSafeArrayAsOutParam(aOvfValues), \
                                  ComSafeArrayAsOutParam(aVBoxValues), \
                                  ComSafeArrayAsOutParam(aExtraConfigValues)); \
    } while (0)

    GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_CloudProfileName);
    if (aVBoxValues.size() == 0)
        return setErrorVrc(VERR_NOT_FOUND, tr("%s: Cloud user profile name wasn't found"), __FUNCTION__);

    Utf8Str profileName(aVBoxValues[0]);
    if (profileName.isEmpty())
        return setErrorVrc(VERR_INVALID_STATE, tr("%s: Cloud user profile name is empty"), __FUNCTION__);

    hrc = cloudProvider->GetProfileByName(aVBoxValues[0], cloudProfile.asOutParam());
    if (FAILED(hrc))
        return setErrorVrc(VERR_COM_OBJECT_NOT_FOUND, tr("%s: Cloud profile object wasn't found"), __FUNCTION__);

    ComObjPtr<ICloudClient> cloudClient;
    hrc = cloudProfile->CreateCloudClient(cloudClient.asOutParam());
    if (FAILED(hrc))
        return setErrorVrc(VERR_COM_OBJECT_NOT_FOUND, tr("%s: Cloud client object wasn't found"), __FUNCTION__);

    ComPtr<IProgress> pProgress;
    hrc = pTask->pProgress.queryInterfaceTo(pProgress.asOutParam());
    if (FAILED(hrc))
        return hrc;

    Utf8Str strOsType;
    ComPtr<IGuestOSType> pGuestOSType;
    {
        VBOXOSTYPE guestOsType = VBOXOSTYPE_Unknown;
        GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_OS); //aVBoxValues is set in this #define
        if (aVBoxValues.size() != 0)
        {
            strOsType = aVBoxValues[0];
            /* Check the OS type */
            uint32_t const idxOSType = Global::getOSTypeIndexFromId(strOsType.c_str());
            guestOsType = idxOSType < Global::cOSTypes ? Global::sOSTypes[idxOSType].osType : VBOXOSTYPE_Unknown;

            /* Case when some invalid OS type or garbage was passed. Set to VBOXOSTYPE_Unknown. */
            if (idxOSType > Global::cOSTypes)
            {
                strOsType = Global::OSTypeId(guestOsType);
                vsd->RemoveDescriptionByType(VirtualSystemDescriptionType_OS);
                vsd->AddDescription(VirtualSystemDescriptionType_OS,
                                    Bstr(strOsType).raw(),
                                    NULL);
            }
        }
        /* Case when no OS type was passed. Set to VBOXOSTYPE_Unknown. */
        else
        {
            strOsType = Global::OSTypeId(guestOsType);
            vsd->AddDescription(VirtualSystemDescriptionType_OS,
                                Bstr(strOsType).raw(),
                                NULL);
        }

        LogRel(("%s: OS type is %s\n", __FUNCTION__, strOsType.c_str()));

        /* We can get some default settings from GuestOSType when it's needed */
        hrc = mVirtualBox->GetGuestOSType(Bstr(strOsType).raw(), pGuestOSType.asOutParam());
        if (FAILED(hrc))
            return hrc;
    }

    /* Should be defined here because it's used later, at least when ComposeMachineFilename() is called */
    Utf8Str strVMName("VM_exported_from_cloud");
    Utf8Str strVMGroup("/");
    Utf8Str strVMBaseFolder;
    Utf8Str strVMSettingFilePath;

    if (m->virtualSystemDescriptions.size() == 1)
    {
        do
        {
            ComPtr<IVirtualBox> VBox(mVirtualBox);

            {
                GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_Name); //aVBoxValues is set in this #define
                if (aVBoxValues.size() != 0)//paranoia but anyway...
                    strVMName = aVBoxValues[0];
                LogRel(("%s: VM name is %s\n", __FUNCTION__, strVMName.c_str()));
            }

//          i_searchUniqueVMName(strVMName);//internally calls setError() in the case of absent the registered VM with such name

            ComPtr<IMachine> machine;
            hrc = mVirtualBox->FindMachine(Bstr(strVMName.c_str()).raw(), machine.asOutParam());
            if (SUCCEEDED(hrc))
            {
                /* what to do? create a new name from the old one with some suffix? */
                uint64_t uRndSuff = RTRandU64();
                vrc = strVMName.appendPrintfNoThrow("__%RU64", uRndSuff);
                AssertRCBreakStmt(vrc, hrc = E_OUTOFMEMORY);

                vsd->RemoveDescriptionByType(VirtualSystemDescriptionType_Name);
                vsd->AddDescription(VirtualSystemDescriptionType_Name,
                                    Bstr(strVMName).raw(),
                                    NULL);
                /* No check again because it would be weird if a VM with such unique name exists */
            }

            Bstr bstrSettingsFilename;
            GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_SettingsFile);
            if (aVBoxValues.size() == 0)
            {
                GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_PrimaryGroup);
                if (aVBoxValues.size() != 0)
                    strVMGroup = aVBoxValues[0];

                GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_BaseFolder);
                if (aVBoxValues.size() != 0)
                    strVMBaseFolder = aVBoxValues[0];

                /* Based on the VM name, create a target machine path. */
                hrc = mVirtualBox->ComposeMachineFilename(Bstr(strVMName).raw(),
                                                          Bstr(strVMGroup).raw(),
                                                          NULL /* aCreateFlags */,
                                                          Bstr(strVMBaseFolder).raw(),
                                                          bstrSettingsFilename.asOutParam());
                if (FAILED(hrc))
                    break;
            }
            else
            {
                bstrSettingsFilename = aVBoxValues[0];
                vsd->AddDescription(VirtualSystemDescriptionType_SettingsFile,
                                    bstrSettingsFilename.raw(),
                                    NULL);
            }

            {
                // CPU count
                GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_CPU);
                if (aVBoxValues.size() == 0)//1 CPU by default
                    vsd->AddDescription(VirtualSystemDescriptionType_CPU,
                                        Bstr("1").raw(),
                                        NULL);

                // RAM
                /* It's alway stored in bytes in VSD according to the old internal agreement within the team */
                GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_Memory);
                if (aVBoxValues.size() == 0)//1024MB by default, 1,073,741,824 in bytes
                    vsd->AddDescription(VirtualSystemDescriptionType_Memory,
                                        Bstr("1073741824").raw(),
                                        NULL);

                // audio adapter
                GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_SoundCard);
//              if (aVBoxValues.size() == 0)
//                  vsd->AddDescription(VirtualSystemDescriptionType_SoundCard,
//                                      Bstr("SB16").raw(),
//                                      NULL);

                //description
                GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_Description);
                if (aVBoxValues.size() == 0)
                    vsd->AddDescription(VirtualSystemDescriptionType_Description,
                                        Bstr("There is no description for this VM").raw(),
                                        NULL);
            }

            {
                Utf8Str strMachineFolder(bstrSettingsFilename);
                strMachineFolder.stripFilename();

                RTFSOBJINFO dirInfo;
                vrc = RTPathQueryInfo(strMachineFolder.c_str(), &dirInfo, RTFSOBJATTRADD_NOTHING);
                if (RT_SUCCESS(vrc))
                {
                    size_t counter = 0;
                    RTDIR hDir;
                    vrc = RTDirOpen(&hDir, strMachineFolder.c_str());
                    if (RT_SUCCESS(vrc))
                    {
                        RTDIRENTRY DirEntry;
                        while (RT_SUCCESS(RTDirRead(hDir, &DirEntry, NULL)))
                        {
                            if (RTDirEntryIsStdDotLink(&DirEntry))
                                continue;
                            ++counter;
                        }

                        if ( hDir != NULL)
                        {
                            vrc = RTDirClose(hDir);
                            AssertRC(vrc);
                        }
                    }
                    else
                        return setErrorVrc(vrc, tr("Can't open folder %s"), strMachineFolder.c_str());

                    if (counter > 0)
                        return setErrorVrc(VERR_ALREADY_EXISTS,
                                           tr("The target folder %s has already contained some files (%d items). Clear the folder from the files or choose another folder"),
                                           strMachineFolder.c_str(), counter);
                }
            }

            GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_CloudInstanceId); //aVBoxValues is set in this #define
            if (aVBoxValues.size() == 0)
                return setErrorVrc(VERR_NOT_FOUND, "%s: Cloud Instance Id wasn't found", __FUNCTION__);

            Utf8Str strInsId = aVBoxValues[0];

            LogRelFunc(("calling CloudClient::ImportInstance\n"));

            /* Here it's strongly supposed that cloud import produces ONE object on the disk.
             * Because it much easier to manage one object in any case.
             * In the case when cloud import creates several object on the disk all of them
             * must be combined together into one object by cloud client.
             * The most simple way is to create a TAR archive. */
            hrc = cloudClient->ImportInstance(m->virtualSystemDescriptions.front(), pProgress);
            if (FAILED(hrc))
            {
                LogRelFunc(("Cloud import (cloud phase) failed. Used cloud instance is \'%s\'\n", strInsId.c_str()));
                hrc = setError(hrc, tr("%s: Cloud import (cloud phase) failed. Used cloud instance is \'%s\'\n"),
                               __FUNCTION__, strInsId.c_str());
                break;
            }

        } while (0);
    }
    else
    {
        hrc = setErrorVrc(VERR_NOT_SUPPORTED, tr("Import from Cloud isn't supported for more than one VM instance."));
        return hrc;
    }


    /* In any case we delete the cloud leavings which may exist after the first phase (cloud phase).
     * Should they be deleted in the OCICloudClient::importInstance()?
     * Because deleting them here is not easy as it in the importInstance(). */
    {
        ErrorInfoKeeper eik;    /* save the error info */
        HRESULT const hrcSaved = hrc;

        GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_CloudInstanceId); //aVBoxValues is set in this #define
        if (aVBoxValues.size() == 0)
            hrc = setErrorVrc(VERR_NOT_FOUND, tr("%s: Cloud cleanup action - the instance wasn't found"), __FUNCTION__);
        else
        {
            vsdData = aVBoxValues[0];

            /** @todo
             *  future function which will eliminate the temporary objects created during the first phase.
             *  hrc = cloud.EliminateImportLeavings(aVBoxValues[0], pProgress); */
/*
            if (FAILED(hrc))
            {
                hrc = setError(hrc, tr("Some leavings may exist in the Cloud."));
                LogRel(("%s: Cleanup action - the leavings in the %s after import the "
                        "instance %s may not have been deleted\n",
                        __FUNCTION__, strProviderName.c_str(), vsdData.c_str()));
            }
            else
                LogRel(("%s: Cleanup action - the leavings in the %s after import the "
                        "instance %s have been deleted\n",
                        __FUNCTION__, strProviderName.c_str(), vsdData.c_str()));
*/
        }

        /* Because during the cleanup phase the hrc may have the good result
         * Thus we restore the original error in the case when the cleanup phase was successful
         * Otherwise we return not the original error but the last error in the cleanup phase */
        /** @todo r=bird: do this conditionally perhaps?
         * if (FAILED(hrcSaved))
         *     hrc = hrcSaved;
         * else
         *     eik.forget();
         */
        hrc = hrcSaved;
    }

    if (FAILED(hrc))
    {
        const char *pszGeneralRollBackErrorMessage = tr("Rollback action for Import Cloud operation failed. "
                                                        "Some leavings may exist on the local disk or in the Cloud.");
        /*
         * Roll-back actions.
         * we finish here if:
         * 1. Getting the object from the Cloud has been failed.
         * 2. Something is wrong with getting data from ComPtr<IVirtualSystemDescription> vsd.
         * 3. More than 1 VirtualSystemDescription is presented in the list m->virtualSystemDescriptions.
         * Maximum what we have there are:
         * 1. The downloaded object, so just check the presence and delete it if one exists
         */

        { /** @todo r=bird: Pointless {}. */
            if (!fKeepDownloadedObject)
            {
                ErrorInfoKeeper eik;    /* save the error info */
                HRESULT const hrcSaved = hrc;

                /* small explanation here, the image here points out to the whole downloaded object (not to the image only)
                 * filled during the first cloud import stage (in the ICloudClient::importInstance()) */
                GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_HardDiskImage); //aVBoxValues is set in this #define
                if (aVBoxValues.size() == 0)
                    hrc = setErrorVrc(VERR_NOT_FOUND, pszGeneralRollBackErrorMessage);
                else
                {
                    vsdData = aVBoxValues[0];
                    //try to delete the downloaded object
                    bool fExist = RTPathExists(vsdData.c_str());
                    if (fExist)
                    {
                        vrc = RTFileDelete(vsdData.c_str());
                        if (RT_FAILURE(vrc))
                        {
                            hrc = setErrorVrc(vrc, pszGeneralRollBackErrorMessage);
                            LogRel(("%s: Rollback action - the object %s hasn't been deleted\n", __FUNCTION__, vsdData.c_str()));
                        }
                        else
                            LogRel(("%s: Rollback action - the object %s has been deleted\n", __FUNCTION__, vsdData.c_str()));
                    }
                }

                /* Because during the rollback phase the hrc may have the good result
                 * Thus we restore the original error in the case when the rollback phase was successful
                 * Otherwise we return not the original error but the last error in the rollback phase */
                hrc = hrcSaved;
            }
        }
    }
    else
    {
        Utf8Str strMachineFolder;
        Utf8Str strAbsSrcPath;
        Utf8Str strGroup("/");//default VM group
        Utf8Str strTargetFormat("VMDK");//default image format
        Bstr bstrSettingsFilename;
        SystemProperties *pSysProps = NULL;
        RTCList<Utf8Str> extraCreatedFiles;/* All extra created files, it's used during cleanup */

        /* Put all VFS* declaration here because they are needed to be release by the corresponding
           RTVfs***Release functions in the case of exception */
        RTVFSOBJ     hVfsObj = NIL_RTVFSOBJ;
        RTVFSFSSTREAM hVfsFssObject = NIL_RTVFSFSSTREAM;
        RTVFSIOSTREAM hVfsIosCurr = NIL_RTVFSIOSTREAM;

        try
        {
            /* Small explanation here, the image here points out to the whole downloaded object (not to the image only)
             * filled during the first cloud import stage (in the ICloudClient::importInstance()) */
            GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_HardDiskImage); //aVBoxValues is set in this #define
            if (aVBoxValues.size() == 0)
                throw setErrorVrc(VERR_NOT_FOUND, "%s: The description of the downloaded object wasn't found", __FUNCTION__);

            strAbsSrcPath = aVBoxValues[0];

            /* Based on the VM name, create a target machine path. */
            hrc = mVirtualBox->ComposeMachineFilename(Bstr(strVMName).raw(),
                                                      Bstr(strGroup).raw(),
                                                      NULL /* aCreateFlags */,
                                                      NULL /* aBaseFolder */,
                                                      bstrSettingsFilename.asOutParam());
            if (FAILED(hrc)) throw hrc;

            strMachineFolder = bstrSettingsFilename;
            strMachineFolder.stripFilename();

            /* Get the system properties. */
            pSysProps = mVirtualBox->i_getSystemProperties();
            if (pSysProps == NULL)
                throw VBOX_E_OBJECT_NOT_FOUND;

            ComObjPtr<MediumFormat> trgFormat;
            trgFormat = pSysProps->i_mediumFormatFromExtension(strTargetFormat);
            if (trgFormat.isNull())
                throw VBOX_E_OBJECT_NOT_FOUND;

            /* Continue and create new VM using data from VSD and downloaded object.
             * The downloaded images should be converted to VDI/VMDK if they have another format */
            Utf8Str strInstId("default cloud instance id");
            GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_CloudInstanceId); //aVBoxValues is set in this #define
            if (aVBoxValues.size() != 0)//paranoia but anyway...
                strInstId = aVBoxValues[0];
            LogRel(("%s: Importing cloud instance %s\n", __FUNCTION__, strInstId.c_str()));

            /* Processing the downloaded object (prepare for the local import) */
            RTVFSIOSTREAM hVfsIosSrc;
            vrc = RTVfsIoStrmOpenNormal(strAbsSrcPath.c_str(), RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE, &hVfsIosSrc);
            if (RT_FAILURE(vrc))
                throw setErrorVrc(vrc, tr("Error opening '%s' for reading (%Rrc)\n"), strAbsSrcPath.c_str(), vrc);

            vrc = RTZipTarFsStreamFromIoStream(hVfsIosSrc, 0 /*fFlags*/, &hVfsFssObject);
            RTVfsIoStrmRelease(hVfsIosSrc);
            if (RT_FAILURE(vrc))
                throw setErrorVrc(vrc, tr("Error reading the downloaded file '%s' (%Rrc)"), strAbsSrcPath.c_str(), vrc);

            /* Create a new virtual system and work directly on the list copy. */
            m->pReader->m_llVirtualSystems.push_back(ovf::VirtualSystem());
            ovf::VirtualSystem &vsys = m->pReader->m_llVirtualSystems.back();

            /* Try to re-use some OVF stuff here */
            {
                vsys.strName = strVMName;
                uint32_t cpus = 1;
                {
                    GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_CPU); //aVBoxValues is set in this #define
                    if (aVBoxValues.size() != 0)
                    {
                        vsdData = aVBoxValues[0];
                        cpus = vsdData.toUInt32();
                    }
                    vsys.cCPUs = (uint16_t)cpus;
                    LogRel(("%s: Number of CPUs is %s\n", __FUNCTION__, vsdData.c_str()));
                }

                ULONG memoryInMB;
                pGuestOSType->COMGETTER(RecommendedRAM)(&memoryInMB);//returned in MB
                uint64_t memoryInBytes = memoryInMB * _1M;//convert to bytes
                {
                    /* It's alway stored in bytes in VSD according to the old internal agreement within the team */
                    GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_Memory); //aVBoxValues is set in this #define
                    if (aVBoxValues.size() != 0)
                    {
                        vsdData = aVBoxValues[0];

                        uint64_t cbRam = vsdData.toUInt64();
                        memoryInBytes = RT_CLAMP(cbRam, (uint64_t)MM_RAM_MIN, (uint64_t)MM_RAM_MAX);
                    }
                    //and set in ovf::VirtualSystem in bytes
                    vsys.ullMemorySize = memoryInBytes;
                    LogRel(("%s: Size of RAM is %d MB\n", __FUNCTION__, vsys.ullMemorySize / _1M));
                }

                {
                    GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_Description); //aVBoxValues is set in this #define
                    if (aVBoxValues.size() != 0)
                    {
                        vsdData = aVBoxValues[0];
                        vsys.strDescription = vsdData;
                    }
                    LogRel(("%s: VM description \'%s\'\n", __FUNCTION__, vsdData.c_str()));
                }

                {
                    GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_OS); //aVBoxValues is set in this #define
                    if (aVBoxValues.size() != 0)
                        strOsType = aVBoxValues[0];
                    vsys.strTypeVBox = strOsType;
                    LogRel(("%s: OS type is %s\n", __FUNCTION__, strOsType.c_str()));
                }

                ovf::EthernetAdapter ea;
                {
                    GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_NetworkAdapter); //aVBoxValues is set in this #define
                    if (aVBoxValues.size() != 0)
                    {
                        ea.strAdapterType = (Utf8Str)(aVBoxValues[0]);
                        ea.strNetworkName = "NAT";//default
                        vsys.llEthernetAdapters.push_back(ea);
                        LogRel(("%s: Network adapter type is %s\n", __FUNCTION__, ea.strAdapterType.c_str()));
                    }
                    else
                    {
                        NetworkAdapterType_T defaultAdapterType = NetworkAdapterType_Am79C970A;
                        pGuestOSType->COMGETTER(AdapterType)(&defaultAdapterType);
                        Utf8StrFmt dat("%RU32", (uint32_t)defaultAdapterType);
                        vsd->AddDescription(VirtualSystemDescriptionType_NetworkAdapter,
                                            Bstr(dat).raw(),
                                            Bstr(Utf8Str("NAT")).raw());
                    }
                }

                ovf::HardDiskController hdc;
                {
                    //It's thought that SATA is supported by any OS types
                    hdc.system = ovf::HardDiskController::SATA;
                    hdc.strIdController = "0";

                    GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_HardDiskControllerSATA); //aVBoxValues is set in this #define
                    if (aVBoxValues.size() != 0)
                        hdc.strControllerType = (Utf8Str)(aVBoxValues[0]);
                    else
                        hdc.strControllerType = "AHCI";

                    LogRel(("%s: Hard disk controller type is %s\n", __FUNCTION__, hdc.strControllerType.c_str()));
                    vsys.mapControllers[hdc.strIdController] = hdc;

                    if (aVBoxValues.size() == 0)
                    {
                        /* we should do it here because it'll be used later in the OVF logic (inside i_importMachines()) */
                        vsd->AddDescription(VirtualSystemDescriptionType_HardDiskControllerSATA,
                                            Bstr(hdc.strControllerType).raw(),
                                            NULL);
                    }
                }

                {
                    GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_SoundCard); //aVBoxValues is set in this #define
                    if (aVBoxValues.size() != 0)
                        vsys.strSoundCardType  = (Utf8Str)(aVBoxValues[0]);
                    else
                    {
                        AudioControllerType_T defaultAudioController;
                        pGuestOSType->COMGETTER(RecommendedAudioController)(&defaultAudioController);
                        vsys.strSoundCardType = Utf8StrFmt("%RU32", (uint32_t)defaultAudioController);//"ensoniq1371";//"AC97";
                        vsd->AddDescription(VirtualSystemDescriptionType_SoundCard,
                                            Bstr(vsys.strSoundCardType).raw(),
                                            NULL);
                    }

                    LogRel(("%s: Sound card is %s\n", __FUNCTION__, vsys.strSoundCardType.c_str()));
                }

                vsys.fHasFloppyDrive = false;
                vsys.fHasCdromDrive = false;
                vsys.fHasUsbController = true;
            }

            unsigned currImageObjectNum = 0;
            hrc = S_OK;
            do
            {
                char *pszName = NULL;
                RTVFSOBJTYPE enmType;
                vrc = RTVfsFsStrmNext(hVfsFssObject, &pszName, &enmType, &hVfsObj);
                if (RT_FAILURE(vrc))
                {
                    if (vrc != VERR_EOF)
                    {
                        hrc = setErrorVrc(vrc, tr("%s: Error reading '%s' (%Rrc)"), __FUNCTION__, strAbsSrcPath.c_str(), vrc);
                        throw hrc;
                    }
                    break;
                }

                /* We only care about entries that are files. Get the I/O stream handle for them. */
                if (   enmType  == RTVFSOBJTYPE_IO_STREAM
                    || enmType  == RTVFSOBJTYPE_FILE)
                {
                    /* Find the suffix and check if this is a possibly interesting file. */
                    char *pszSuffix = RTStrToLower(strrchr(pszName, '.'));

                    /* Get the I/O stream. */
                    hVfsIosCurr = RTVfsObjToIoStream(hVfsObj);
                    Assert(hVfsIosCurr != NIL_RTVFSIOSTREAM);

                    /* Get the source medium format */
                    ComObjPtr<MediumFormat> srcFormat;
                    srcFormat = pSysProps->i_mediumFormatFromExtension(pszSuffix + 1);

                    /* unknown image format so just extract a file without any processing */
                    if (srcFormat == NULL)
                    {
                        /* Read the file into a memory buffer */
                        void  *pvBuffered;
                        size_t cbBuffered;
                        RTVFSFILE hVfsDstFile = NIL_RTVFSFILE;
                        try
                        {
                            vrc = RTVfsIoStrmReadAll(hVfsIosCurr, &pvBuffered, &cbBuffered);
                            RTVfsIoStrmRelease(hVfsIosCurr);
                            hVfsIosCurr = NIL_RTVFSIOSTREAM;
                            if (RT_FAILURE(vrc))
                                throw  setErrorVrc(vrc, tr("Could not read the file '%s' (%Rrc)"), strAbsSrcPath.c_str(), vrc);

                            Utf8StrFmt strAbsDstPath("%s%s%s", strMachineFolder.c_str(), RTPATH_SLASH_STR, pszName);

                            /* Simple logic - just try to get dir info, in case of absent try to create one.
                               No deep errors analysis */
                            RTFSOBJINFO dirInfo;
                            vrc = RTPathQueryInfo(strMachineFolder.c_str(), &dirInfo, RTFSOBJATTRADD_NOTHING);
                            if (RT_FAILURE(vrc))
                            {
                                if (vrc == VERR_FILE_NOT_FOUND || vrc == VERR_PATH_NOT_FOUND)
                                {
                                    vrc = RTDirCreateFullPath(strMachineFolder.c_str(), 0755);
                                    if (RT_FAILURE(vrc))
                                        throw  setErrorVrc(vrc, tr("Could not create the directory '%s' (%Rrc)"),
                                                           strMachineFolder.c_str(), vrc);
                                }
                                else
                                    throw  setErrorVrc(vrc, tr("Error during getting info about the directory '%s' (%Rrc)"),
                                                       strMachineFolder.c_str(), vrc);
                            }

                            /* Write the file on the disk */
                            vrc = RTVfsFileOpenNormal(strAbsDstPath.c_str(),
                                                      RTFILE_O_WRITE | RTFILE_O_DENY_ALL | RTFILE_O_CREATE,
                                                      &hVfsDstFile);
                            if (RT_FAILURE(vrc))
                                throw  setErrorVrc(vrc, tr("Could not create the file '%s' (%Rrc)"), strAbsDstPath.c_str(), vrc);

                            size_t cbWritten;
                            vrc = RTVfsFileWrite(hVfsDstFile, pvBuffered, cbBuffered, &cbWritten);
                            if (RT_FAILURE(vrc))
                                throw  setErrorVrc(vrc, tr("Could not write into the file '%s' (%Rrc)"), strAbsDstPath.c_str(), vrc);

                            /* Remember this file */
                            extraCreatedFiles.append(strAbsDstPath);
                        }
                        catch (HRESULT aRc)
                        {
                            hrc = aRc;
                            LogRel(("%s: Processing the downloaded object was failed. The exception (%Rhrc)\n",
                                    __FUNCTION__, hrc));
                        }
                        catch (int aRc)
                        {
                            hrc = setErrorVrc(aRc);
                            LogRel(("%s: Processing the downloaded object was failed. The exception (%Rrc/%Rhrc)\n",
                                    __FUNCTION__, aRc, hrc));
                        }
                        catch (...)
                        {
                            hrc = setErrorVrc(VERR_UNEXPECTED_EXCEPTION);
                            LogRel(("%s: Processing the downloaded object was failed. The exception (VERR_UNEXPECTED_EXCEPTION/%Rhrc)\n",
                                    __FUNCTION__, hrc));
                        }
                    }
                    else
                    {
                        /* Just skip the rest images if they exist. Only the first image is used as the base image. */
                        if (currImageObjectNum >= 1)
                            continue;

                        /* Image format is supported by VBox so extract the file and try to convert
                         * one to the default format (which is VMDK for now) */
                        Utf8Str z(bstrSettingsFilename);
                        Utf8StrFmt strAbsDstPath("%s_%d.%s",
                                     z.stripSuffix().c_str(),
                                     currImageObjectNum,
                                     strTargetFormat.c_str());

                        hrc = mVirtualBox->i_findHardDiskByLocation(strAbsDstPath, false, NULL);
                        if (SUCCEEDED(hrc))
                            throw setErrorVrc(VERR_ALREADY_EXISTS, tr("The hard disk '%s' already exists."), strAbsDstPath.c_str());

                        /* Create an IMedium object. */
                        ComObjPtr<Medium> pTargetMedium;
                        pTargetMedium.createObject();
                        hrc = pTargetMedium->init(mVirtualBox,
                                                 strTargetFormat,
                                                 strAbsDstPath,
                                                 Guid::Empty /* media registry: none yet */,
                                                 DeviceType_HardDisk);
                        if (FAILED(hrc))
                            throw hrc;

                        pTask->pProgress->SetNextOperation(BstrFmt(tr("Importing virtual disk image '%s'"), pszName).raw(),
                                                           200);
                        ComObjPtr<Medium> nullParent;
                        ComPtr<IProgress> pProgressImport;
                        ComObjPtr<Progress> pProgressImportTmp;
                        hrc = pProgressImportTmp.createObject();
                        if (FAILED(hrc))
                            throw hrc;

                        hrc = pProgressImportTmp->init(mVirtualBox,
                                                       static_cast<IAppliance*>(this),
                                                       Utf8StrFmt(tr("Importing medium '%s'"), pszName),
                                                       TRUE);
                        if (FAILED(hrc))
                            throw hrc;

                        pProgressImportTmp.queryInterfaceTo(pProgressImport.asOutParam());

                        hrc = pTargetMedium->i_importFile(pszName,
                                                          srcFormat,
                                                          MediumVariant_Standard,
                                                          hVfsIosCurr,
                                                          nullParent,
                                                          pProgressImportTmp,
                                                          true /* aNotify */);
                        RTVfsIoStrmRelease(hVfsIosCurr);
                        hVfsIosCurr = NIL_RTVFSIOSTREAM;
                        /* Now wait for the background import operation to complete;
                         * this throws HRESULTs on error. */
                        hrc = pTask->pProgress->WaitForOtherProgressCompletion(pProgressImport, 0 /* indefinite wait */);

                        /* Try to re-use some OVF stuff here */
                        if (SUCCEEDED(hrc))
                        {
                            /* Small trick here.
                             * We add new item into the actual VSD after successful conversion.
                             * There is no need to delete any previous records describing the images in the VSD
                             * because later in the code the search of the images in the VSD will use such records
                             * with the actual image id (d.strDiskId = pTargetMedium->i_getId().toString()) which is
                             * used as a key for m->pReader->m_mapDisks, vsys.mapVirtualDisks.
                             * So all 3 objects are tied via the image id.
                             * In the OVF case we already have all such records in the VSD after reading OVF
                             * description file (read() and interpret() functions).*/
                            ovf::DiskImage d;
                            d.strDiskId = pTargetMedium->i_getId().toString();
                            d.strHref = pTargetMedium->i_getLocationFull();
                            d.strFormat = pTargetMedium->i_getFormat();
                            d.iSize = (int64_t)pTargetMedium->i_getSize();
                            d.ulSuggestedSizeMB = (uint32_t)(d.iSize/_1M);

                            m->pReader->m_mapDisks[d.strDiskId] = d;

                            ComObjPtr<VirtualSystemDescription> vsdescThis = m->virtualSystemDescriptions.front();

                            /* It's needed here to use the internal function i_addEntry() instead of the API function
                             * addDescription() because we should pass the d.strDiskId for the proper handling this
                             * disk later in the i_importMachineGeneric():
                             * - find the line like this "if (vsdeHD->strRef == diCurrent.strDiskId)".
                             *  if those code can be eliminated then addDescription() will be used. */
                            vsdescThis->i_addEntry(VirtualSystemDescriptionType_HardDiskImage,
                                                   d.strDiskId,
                                                   d.strHref,
                                                   d.strHref,
                                                   d.ulSuggestedSizeMB);

                            ovf::VirtualDisk vd;
                            //next line may generates std::out_of_range exception in case of failure
                            vd.strIdController = vsys.mapControllers.at("0").strIdController;
                            vd.ulAddressOnParent = 0;
                            vd.strDiskId = d.strDiskId;
                            vsys.mapVirtualDisks[vd.strDiskId] = vd;

                            ++currImageObjectNum;
                        }
                    }

                    RTVfsIoStrmRelease(hVfsIosCurr);
                    hVfsIosCurr = NIL_RTVFSIOSTREAM;
                }

                RTVfsObjRelease(hVfsObj);
                hVfsObj = NIL_RTVFSOBJ;

                RTStrFree(pszName);

            } while (SUCCEEDED(hrc));

            RTVfsFsStrmRelease(hVfsFssObject);
            hVfsFssObject = NIL_RTVFSFSSTREAM;

            if (SUCCEEDED(hrc))
            {
                pTask->pProgress->SetNextOperation(BstrFmt(tr("Creating new VM '%s'"), strVMName.c_str()).raw(), 50);
                /* Create the import stack to comply OVF logic.
                 * Before we filled some other data structures which are needed by OVF logic too.*/
                ImportStack stack(pTask->locInfo, m->pReader->m_mapDisks, pTask->pProgress, NIL_RTVFSFSSTREAM);
                i_importMachines(stack);
            }

        }
        catch (HRESULT aRc)
        {
            hrc = aRc;
            LogRel(("%s: Cloud import (local phase) failed. The exception (%Rhrc)\n",
                    __FUNCTION__, hrc));
        }
        catch (int aRc)
        {
            hrc = setErrorVrc(aRc);
            LogRel(("%s: Cloud import (local phase) failed. The exception (%Rrc/%Rhrc)\n",
                    __FUNCTION__, aRc, hrc));
        }
        catch (...)
        {
            hrc = setErrorVrc(VERR_UNRESOLVED_ERROR);
            LogRel(("%s: Cloud import (local phase) failed. The exception (VERR_UNRESOLVED_ERROR/%Rhrc)\n",
                    __FUNCTION__, hrc));
        }

        LogRel(("%s: Cloud import (local phase) final result (%Rrc).\n", __FUNCTION__, hrc));

        /* Try to free VFS stuff because some of them might not be released due to the exception */
        if (hVfsIosCurr != NIL_RTVFSIOSTREAM)
            RTVfsIoStrmRelease(hVfsIosCurr);
        if (hVfsObj != NIL_RTVFSOBJ)
            RTVfsObjRelease(hVfsObj);
        if (hVfsFssObject != NIL_RTVFSFSSTREAM)
            RTVfsFsStrmRelease(hVfsFssObject);

        /* Small explanation here.
         * After adding extracted files into the actual VSD the returned list will contain not only the
         * record about the downloaded object but also the records about the extracted files from this object.
         * It's needed to go through this list to find the record about the downloaded object.
         * But it was the first record added into the list, so aVBoxValues[0] should be correct here.
         */
        GET_VSD_DESCRIPTION_BY_TYPE(VirtualSystemDescriptionType_HardDiskImage); //aVBoxValues is set in this #define
        if (!fKeepDownloadedObject)
        {
            if (aVBoxValues.size() != 0)
            {
                vsdData = aVBoxValues[0];
                //try to delete the downloaded object
                bool fExist = RTPathExists(vsdData.c_str());
                if (fExist)
                {
                    vrc = RTFileDelete(vsdData.c_str());
                    if (RT_FAILURE(vrc))
                        LogRel(("%s: Cleanup action - the downloaded object %s hasn't been deleted\n", __FUNCTION__, vsdData.c_str()));
                    else
                        LogRel(("%s: Cleanup action - the downloaded object %s has been deleted\n", __FUNCTION__, vsdData.c_str()));
                }
            }
        }

        if (FAILED(hrc))
        {
            /* What to do here?
             * For now:
             *  - check the registration of created VM and delete one.
             *  - check the list of imported images, detach them and next delete if they have still registered in the VBox.
             *  - check some other leavings and delete them if they exist.
             */

            /* It's not needed to call "pTask->pProgress->SetNextOperation(BstrFmt("The cleanup phase").raw(), 50)" here
             * because, WaitForOtherProgressCompletion() calls the SetNextOperation() iternally.
             * At least, it's strange that the operation description is set to the previous value. */

            ComPtr<IMachine> pMachine;
            Utf8Str machineNameOrId = strVMName;

            /* m->llGuidsMachinesCreated is filled in the i_importMachineGeneric()/i_importVBoxMachine()
             * after successful registration of new VM */
            if (!m->llGuidsMachinesCreated.empty())
                machineNameOrId = m->llGuidsMachinesCreated.front().toString();

            hrc = mVirtualBox->FindMachine(Bstr(machineNameOrId).raw(), pMachine.asOutParam());

            if (SUCCEEDED(hrc))
            {
                LogRel(("%s: Cleanup action - the VM with the name(or id) %s was found\n", __FUNCTION__, machineNameOrId.c_str()));
                SafeIfaceArray<IMedium> aMedia;
                hrc = pMachine->Unregister(CleanupMode_DetachAllReturnHardDisksOnly, ComSafeArrayAsOutParam(aMedia));
                if (SUCCEEDED(hrc))
                {
                    LogRel(("%s: Cleanup action - the VM %s has been unregistered\n", __FUNCTION__, machineNameOrId.c_str()));
                    ComPtr<IProgress> pProgress1;
                    hrc = pMachine->DeleteConfig(ComSafeArrayAsInParam(aMedia), pProgress1.asOutParam());
                    pTask->pProgress->WaitForOtherProgressCompletion(pProgress1, 0 /* indefinite wait */);

                    LogRel(("%s: Cleanup action - the VM config file and the attached images have been deleted\n",
                            __FUNCTION__));
                }
            }
            else
            {
                /* Re-check the items in the array with the images names (paths).
                 * if the import fails before creation VM, then VM won't be found
                 * -> VM can't be unregistered and the images can't be deleted.
                 * The rest items in the array aVBoxValues are the images which might
                 * have still been registered in the VBox.
                 * So go through the array and detach-unregister-delete those images */

                /* have to get write lock as the whole find/update sequence must be done
                 * in one critical section, otherwise there are races which can lead to
                 * multiple Medium objects with the same content */

                AutoWriteLock treeLock(mVirtualBox->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

                for (size_t i = 1; i < aVBoxValues.size(); ++i)
                {
                    vsdData = aVBoxValues[i];
                    ComObjPtr<Medium> poHardDisk;
                    hrc = mVirtualBox->i_findHardDiskByLocation(vsdData, false, &poHardDisk);
                    if (SUCCEEDED(hrc))
                    {
                        hrc = mVirtualBox->i_unregisterMedium((Medium*)(poHardDisk));
                        if (SUCCEEDED(hrc))
                        {
                            ComPtr<IProgress> pProgress1;
                            hrc = poHardDisk->DeleteStorage(pProgress1.asOutParam());
                            pTask->pProgress->WaitForOtherProgressCompletion(pProgress1, 0 /* indefinite wait */);
                        }
                        if (SUCCEEDED(hrc))
                            LogRel(("%s: Cleanup action - the image %s has been deleted\n", __FUNCTION__, vsdData.c_str()));
                    }
                    else if (hrc == VBOX_E_OBJECT_NOT_FOUND)
                    {
                        LogRel(("%s: Cleanup action - the image %s wasn't found. Nothing to delete.\n", __FUNCTION__, vsdData.c_str()));
                        hrc = S_OK;
                    }

                }
            }

            /* Deletion of all additional files which were created during unpacking the downloaded object */
            for (size_t i = 0; i < extraCreatedFiles.size(); ++i)
            {
                vrc = RTFileDelete(extraCreatedFiles.at(i).c_str());
                if (RT_FAILURE(vrc))
                    hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc);
                else
                    LogRel(("%s: Cleanup action - file %s has been deleted\n", __FUNCTION__, extraCreatedFiles.at(i).c_str()));
            }

            /* Deletion of the other files in the VM folder and the folder itself */
            {
                RTDIR   hDir;
                vrc = RTDirOpen(&hDir, strMachineFolder.c_str());
                if (RT_SUCCESS(vrc))
                {
                    for (;;)
                    {
                        RTDIRENTRYEX Entry;
                        vrc = RTDirReadEx(hDir, &Entry, NULL /*pcbDirEntry*/, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                        if (RT_FAILURE(vrc))
                        {
                            AssertLogRelMsg(vrc == VERR_NO_MORE_FILES, ("%Rrc\n", vrc));
                            break;
                        }
                        if (RTFS_IS_FILE(Entry.Info.Attr.fMode))
                        {
                            vrc = RTFileDelete(Entry.szName);
                            if (RT_FAILURE(vrc))
                                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc);
                            else
                                LogRel(("%s: Cleanup action - file %s has been deleted\n", __FUNCTION__, Entry.szName));
                        }
                    }
                    RTDirClose(hDir);
                }

                vrc = RTDirRemove(strMachineFolder.c_str());
                if (RT_FAILURE(vrc))
                    hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc);
            }

            if (FAILED(hrc))
                LogRel(("%s: Cleanup action - some leavings still may exist in the folder %s\n",
                        __FUNCTION__, strMachineFolder.c_str()));
        }
        else
        {
            /* See explanation in the Appliance::i_importImpl() where Progress was setup */
            ULONG operationCount;
            ULONG currOperation;
            pTask->pProgress->COMGETTER(OperationCount)(&operationCount);
            pTask->pProgress->COMGETTER(Operation)(&currOperation);
            while (++currOperation < operationCount)
            {
                pTask->pProgress->SetNextOperation(BstrFmt("Skipping the cleanup phase. All right.").raw(), 1);
                LogRel(("%s: Skipping the cleanup step %d\n", __FUNCTION__, currOperation));
            }
        }
    }

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    LogFlowFuncLeave();
    return hrc;
}

/**
 * Actual worker code for reading an OVF from disk. This is called from Appliance::taskThreadImportOrExport()
 * and therefore runs on the OVF read worker thread. This opens the OVF with ovfreader.cpp.
 *
 * This runs in one context:
 *
 * 1) in a first worker thread; in that case, Appliance::Read() called Appliance::readImpl();
 *
 * @param pTask
 * @return
 */
HRESULT Appliance::i_readFS(TaskOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc;
    if (pTask->locInfo.strPath.endsWith(".ovf", Utf8Str::CaseInsensitive))
        hrc = i_readFSOVF(pTask);
    else
        hrc = i_readFSOVA(pTask);

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    LogFlowFuncLeave();

    return hrc;
}

HRESULT Appliance::i_readFSOVF(TaskOVF *pTask)
{
    LogFlowFunc(("'%s'\n", pTask->locInfo.strPath.c_str()));

    /*
     * Allocate a buffer for filenames and prep it for suffix appending.
     */
    char *pszNameBuf = (char *)alloca(pTask->locInfo.strPath.length() + 16);
    AssertReturn(pszNameBuf, E_OUTOFMEMORY);
    memcpy(pszNameBuf, pTask->locInfo.strPath.c_str(), pTask->locInfo.strPath.length() + 1);
    RTPathStripSuffix(pszNameBuf);
    size_t const cchBaseName = strlen(pszNameBuf);

    /*
     * Open the OVF file first since that is what this is all about.
     */
    RTVFSIOSTREAM hIosOvf;
    int vrc = RTVfsIoStrmOpenNormal(pTask->locInfo.strPath.c_str(),
                                    RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE, &hIosOvf);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Failed to open OVF file '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

    HRESULT hrc = i_readOVFFile(pTask, hIosOvf, RTPathFilename(pTask->locInfo.strPath.c_str())); /* consumes hIosOvf */
    if (FAILED(hrc))
        return hrc;

    /*
     * Try open the manifest file (for signature purposes and to determine digest type(s)).
     */
    RTVFSIOSTREAM hIosMf;
    strcpy(&pszNameBuf[cchBaseName], ".mf");
    vrc = RTVfsIoStrmOpenNormal(pszNameBuf, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE, &hIosMf);
    if (RT_SUCCESS(vrc))
    {
        const char * const pszFilenamePart = RTPathFilename(pszNameBuf);
        hrc = i_readManifestFile(pTask, hIosMf /*consumed*/, pszFilenamePart);
        if (FAILED(hrc))
            return hrc;

        /*
         * Check for the signature file.
         */
        RTVFSIOSTREAM hIosCert;
        strcpy(&pszNameBuf[cchBaseName], ".cert");
        vrc = RTVfsIoStrmOpenNormal(pszNameBuf, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE, &hIosCert);
        if (RT_SUCCESS(vrc))
        {
            hrc = i_readSignatureFile(pTask, hIosCert /*consumed*/, pszFilenamePart);
            if (FAILED(hrc))
                return hrc;
        }
        else if (vrc != VERR_FILE_NOT_FOUND && vrc != VERR_PATH_NOT_FOUND)
            return setErrorVrc(vrc, tr("Failed to open the signature file '%s' (%Rrc)"), pszNameBuf, vrc);

    }
    else if (vrc == VERR_FILE_NOT_FOUND || vrc == VERR_PATH_NOT_FOUND)
    {
        m->fDeterminedDigestTypes = true;
        m->fDigestTypes           = 0;
    }
    else
        return setErrorVrc(vrc, tr("Failed to open the manifest file '%s' (%Rrc)"), pszNameBuf, vrc);

    /*
     * Do tail processing (check the signature).
     */
    hrc = i_readTailProcessing(pTask);

    LogFlowFunc(("returns %Rhrc\n", hrc));
    return hrc;
}

HRESULT Appliance::i_readFSOVA(TaskOVF *pTask)
{
    LogFlowFunc(("'%s'\n", pTask->locInfo.strPath.c_str()));

    /*
     * Open the tar file as file stream.
     */
    RTVFSIOSTREAM hVfsIosOva;
    int vrc = RTVfsIoStrmOpenNormal(pTask->locInfo.strPath.c_str(),
                                    RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsIosOva);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Error opening the OVA file '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

    RTVFSFSSTREAM hVfsFssOva;
    vrc = RTZipTarFsStreamFromIoStream(hVfsIosOva, 0 /*fFlags*/, &hVfsFssOva);
    RTVfsIoStrmRelease(hVfsIosOva);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Error reading the OVA file '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

    /*
     * Since jumping thru an OVA file with seekable disk backing is rather
     * efficient, we can process .ovf, .mf and .cert files here without any
     * strict ordering restrictions.
     *
     * (Technically, the .ovf-file comes first, while the manifest and its
     * optional signature file either follows immediately or at the very end of
     * the OVA. The manifest is optional.)
     */
    char    *pszOvfNameBase = NULL;
    unsigned cLeftToFind = 3;
    HRESULT  hrc = S_OK;
    do
    {
        char        *pszName = NULL;
        RTVFSOBJTYPE enmType;
        RTVFSOBJ     hVfsObj;
        vrc = RTVfsFsStrmNext(hVfsFssOva, &pszName, &enmType, &hVfsObj);
        if (RT_FAILURE(vrc))
        {
            if (vrc != VERR_EOF)
                hrc = setErrorVrc(vrc, tr("Error reading OVA '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);
            break;
        }

        /* We only care about entries that are files. Get the I/O stream handle for them. */
        if (   enmType  == RTVFSOBJTYPE_IO_STREAM
            || enmType  == RTVFSOBJTYPE_FILE)
        {
            /* Find the suffix and check if this is a possibly interesting file. */
            char *pszSuffix = strrchr(pszName, '.');
            if (   pszSuffix
                && (   RTStrICmp(pszSuffix + 1, "ovf") == 0
                    || RTStrICmp(pszSuffix + 1, "mf") == 0
                    || RTStrICmp(pszSuffix + 1, "cert") == 0) )
            {
                /* Match the OVF base name. */
                *pszSuffix = '\0';
                if (   pszOvfNameBase == NULL
                    || RTStrICmp(pszName, pszOvfNameBase) == 0)
                {
                    *pszSuffix = '.';

                    /* Since we're pretty sure we'll be processing this file, get the I/O stream. */
                    RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
                    Assert(hVfsIos != NIL_RTVFSIOSTREAM);

                    /* Check for the OVF (should come first). */
                    if (RTStrICmp(pszSuffix + 1, "ovf") == 0)
                    {
                        if (pszOvfNameBase == NULL)
                        {
                            hrc = i_readOVFFile(pTask, hVfsIos, pszName);
                            hVfsIos = NIL_RTVFSIOSTREAM;

                            /* Set the base name. */
                            *pszSuffix = '\0';
                            pszOvfNameBase = pszName;
                            pszName = NULL;
                            cLeftToFind--;
                        }
                        else
                            LogRel(("i_readFSOVA: '%s' contains more than one OVF file ('%s'), picking the first one\n",
                                    pTask->locInfo.strPath.c_str(), pszName));
                    }
                    /* Check for manifest. */
                    else if (RTStrICmp(pszSuffix + 1, "mf") == 0)
                    {
                        if (m->hMemFileTheirManifest == NIL_RTVFSFILE)
                        {
                            hrc = i_readManifestFile(pTask, hVfsIos, pszName);
                            hVfsIos = NIL_RTVFSIOSTREAM;  /*consumed*/
                            cLeftToFind--;
                        }
                        else
                            LogRel(("i_readFSOVA: '%s' contains more than one manifest file ('%s'), picking the first one\n",
                                    pTask->locInfo.strPath.c_str(), pszName));
                    }
                    /* Check for signature. */
                    else if (RTStrICmp(pszSuffix + 1, "cert") == 0)
                    {
                        if (!m->fSignerCertLoaded)
                        {
                            hrc = i_readSignatureFile(pTask, hVfsIos, pszName);
                            hVfsIos = NIL_RTVFSIOSTREAM;  /*consumed*/
                            cLeftToFind--;
                        }
                        else
                            LogRel(("i_readFSOVA: '%s' contains more than one signature file ('%s'), picking the first one\n",
                                    pTask->locInfo.strPath.c_str(), pszName));
                    }
                    else
                        AssertFailed();
                    if (hVfsIos != NIL_RTVFSIOSTREAM)
                        RTVfsIoStrmRelease(hVfsIos);
                }
            }
        }
        RTVfsObjRelease(hVfsObj);
        RTStrFree(pszName);
    } while (cLeftToFind > 0 && SUCCEEDED(hrc));

    RTVfsFsStrmRelease(hVfsFssOva);
    RTStrFree(pszOvfNameBase);

    /*
     * Check that we found and OVF file.
     */
    if (SUCCEEDED(hrc) && !pszOvfNameBase)
        hrc = setError(VBOX_E_FILE_ERROR, tr("OVA '%s' does not contain an .ovf-file"), pTask->locInfo.strPath.c_str());
    if (SUCCEEDED(hrc))
    {
        /*
         * Do tail processing (check the signature).
         */
        hrc = i_readTailProcessing(pTask);
    }
    LogFlowFunc(("returns %Rhrc\n", hrc));
    return hrc;
}

/**
 * Reads & parses the OVF file.
 *
 * @param   pTask               The read task.
 * @param   hVfsIosOvf          The I/O stream for the OVF.  The reference is
 *                              always consumed.
 * @param   pszManifestEntry    The manifest entry name.
 * @returns COM status code, error info set.
 * @throws  Nothing
 */
HRESULT Appliance::i_readOVFFile(TaskOVF *pTask, RTVFSIOSTREAM hVfsIosOvf, const char *pszManifestEntry)
{
    LogFlowFunc(("%s[%s]\n", pTask->locInfo.strPath.c_str(), pszManifestEntry));

    /*
     * Set the OVF manifest entry name (needed for tweaking the manifest
     * validation during import).
     */
    try         { m->strOvfManifestEntry = pszManifestEntry; }
    catch (...) { return E_OUTOFMEMORY; }

    /*
     * Set up digest calculation.
     */
    hVfsIosOvf = i_manifestSetupDigestCalculationForGivenIoStream(hVfsIosOvf, pszManifestEntry);
    if (hVfsIosOvf == NIL_RTVFSIOSTREAM)
        return VBOX_E_FILE_ERROR;

    /*
     * Read the OVF into a memory buffer and parse it.
     */
    void  *pvBufferedOvf;
    size_t cbBufferedOvf;
    int vrc = RTVfsIoStrmReadAll(hVfsIosOvf, &pvBufferedOvf, &cbBufferedOvf);
    /*
     * Get rid of trailing zeros if xml is encoded in UTF-8 only. This is needed because
     * starting with libxml2 2.12.6 the parser will complain about extra zeroes beyond
     * the xml string content.
     */
    size_t cbValidXmlBufLen = cbBufferedOvf;
    if (*(uint8_t*)pvBufferedOvf == 0x3C)
        cbValidXmlBufLen = RTStrNLen((const char *)pvBufferedOvf, cbBufferedOvf);
    uint32_t cRefs = RTVfsIoStrmRelease(hVfsIosOvf);     /* consumes stream handle.  */
    NOREF(cRefs);
    Assert(cRefs == 0);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Could not read the OVF file for '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

    HRESULT hrc;
    try
    {
        m->pReader = new ovf::OVFReader(pvBufferedOvf, cbValidXmlBufLen, pTask->locInfo.strPath);
        hrc = S_OK;
    }
    catch (RTCError &rXcpt)      // includes all XML exceptions
    {
        hrc = setError(VBOX_E_FILE_ERROR, rXcpt.what());
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }
    catch (...)
    {
        hrc = E_FAIL;
    }
    LogFlowFunc(("OVFReader(%s) -> hrc=%Rhrc\n", pTask->locInfo.strPath.c_str(), hrc));

    RTVfsIoStrmReadAllFree(pvBufferedOvf, cbBufferedOvf);
    if (SUCCEEDED(hrc))
    {
        /*
         * If we see an OVF v2.0 envelope, select only the SHA-256 digest.
         */
        if (   !m->fDeterminedDigestTypes
            && m->pReader->m_envelopeData.getOVFVersion() == ovf::OVFVersion_2_0)
            m->fDigestTypes &= ~RTMANIFEST_ATTR_SHA256;
    }

    return hrc;
}

/**
 * Reads & parses the manifest file.
 *
 * @param   pTask               The read task.
 * @param   hVfsIosMf           The I/O stream for the manifest file.  The
 *                              reference is always consumed.
 * @param   pszSubFileNm        The manifest filename (no path) for error
 *                              messages and logging.
 * @returns COM status code, error info set.
 * @throws  Nothing
 */
HRESULT Appliance::i_readManifestFile(TaskOVF *pTask, RTVFSIOSTREAM hVfsIosMf, const char *pszSubFileNm)
{
    LogFlowFunc(("%s[%s]\n", pTask->locInfo.strPath.c_str(), pszSubFileNm));

    /*
     * Copy the manifest into a memory backed file so we can later do signature
     * validation independent of the algorithms used by the signature.
     */
    int vrc = RTVfsMemorizeIoStreamAsFile(hVfsIosMf, RTFILE_O_READ, &m->hMemFileTheirManifest);
    RTVfsIoStrmRelease(hVfsIosMf);     /* consumes stream handle.  */
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Error reading the manifest file '%s' for '%s' (%Rrc)"),
                           pszSubFileNm, pTask->locInfo.strPath.c_str(), vrc);

    /*
     * Parse the manifest.
     */
    Assert(m->hTheirManifest == NIL_RTMANIFEST);
    vrc = RTManifestCreate(0 /*fFlags*/, &m->hTheirManifest);
    AssertStmt(RT_SUCCESS(vrc), Global::vboxStatusCodeToCOM(vrc));

    char szErr[256];
    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(m->hMemFileTheirManifest);
    vrc = RTManifestReadStandardEx(m->hTheirManifest, hVfsIos, szErr, sizeof(szErr));
    RTVfsIoStrmRelease(hVfsIos);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Failed to parse manifest file '%s' for '%s' (%Rrc): %s"),
                           pszSubFileNm, pTask->locInfo.strPath.c_str(), vrc, szErr);

    /*
     * Check which digest files are used.
     * Note! the file could be empty, in which case fDigestTypes is set to 0.
     */
    vrc = RTManifestQueryAllAttrTypes(m->hTheirManifest, true /*fEntriesOnly*/, &m->fDigestTypes);
    AssertRCReturn(vrc, Global::vboxStatusCodeToCOM(vrc));
    m->fDeterminedDigestTypes = true;

    return S_OK;
}

/**
 * Reads the signature & certificate file.
 *
 * @param   pTask               The read task.
 * @param   hVfsIosCert         The I/O stream for the signature file.  The
 *                              reference is always consumed.
 * @param   pszSubFileNm        The signature filename (no path) for error
 *                              messages and logging.  Used to construct
 *                              .mf-file name.
 * @returns COM status code, error info set.
 * @throws  Nothing
 */
HRESULT Appliance::i_readSignatureFile(TaskOVF *pTask, RTVFSIOSTREAM hVfsIosCert, const char *pszSubFileNm)
{
    LogFlowFunc(("%s[%s]\n", pTask->locInfo.strPath.c_str(), pszSubFileNm));

    /*
     * Construct the manifest filename from pszSubFileNm.
     */
    Utf8Str strManifestName;
    try
    {
        const char *pszSuffix = strrchr(pszSubFileNm, '.');
        AssertReturn(pszSuffix, E_FAIL);
        strManifestName = Utf8Str(pszSubFileNm, (size_t)(pszSuffix - pszSubFileNm));
        strManifestName.append(".mf");
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Copy the manifest into a memory buffer.  We'll do the signature processing
     * later to not force any specific order in the OVAs or any other archive we
     * may be accessing later.
     */
    void  *pvSignature;
    size_t cbSignature;
    int vrc = RTVfsIoStrmReadAll(hVfsIosCert, &pvSignature, &cbSignature);
    RTVfsIoStrmRelease(hVfsIosCert);     /* consumes stream handle.  */
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Error reading the signature file '%s' for '%s' (%Rrc)"),
                           pszSubFileNm, pTask->locInfo.strPath.c_str(), vrc);

    /*
     * Parse the signing certificate. Unlike the manifest parser we use below,
     * this API ignores parts of the file that aren't relevant.
     */
    RTERRINFOSTATIC StaticErrInfo;
    vrc = RTCrX509Certificate_ReadFromBuffer(&m->SignerCert, pvSignature, cbSignature,
                                             RTCRX509CERT_READ_F_PEM_ONLY,
                                             &g_RTAsn1DefaultAllocator, RTErrInfoInitStatic(&StaticErrInfo), pszSubFileNm);
    HRESULT hrc;
    if (RT_SUCCESS(vrc))
    {
        m->fSignerCertLoaded = true;
        m->fCertificateIsSelfSigned = RTCrX509Certificate_IsSelfSigned(&m->SignerCert);

        /*
         * Find the start of the certificate part of the file, so we can avoid
         * upsetting the manifest parser with it.
         */
        char *pszSplit = (char *)RTCrPemFindFirstSectionInContent(pvSignature, cbSignature,
                                                                  g_aRTCrX509CertificateMarkers, g_cRTCrX509CertificateMarkers);
        if (pszSplit)
            while (   pszSplit != (char *)pvSignature
                   && pszSplit[-1] != '\n'
                   && pszSplit[-1] != '\r')
                pszSplit--;
        else
        {
            AssertLogRelMsgFailed(("Failed to find BEGIN CERTIFICATE markers in '%s'::'%s' - impossible unless it's a DER encoded certificate!",
                                   pTask->locInfo.strPath.c_str(), pszSubFileNm));
            pszSplit = (char *)pvSignature + cbSignature;
        }
        char const chSaved = *pszSplit;
        *pszSplit = '\0';

        /*
         * Now, read the manifest part.  We use the IPRT manifest reader here
         * to avoid duplicating code and be somewhat flexible wrt the digest
         * type choosen by the signer.
         */
        RTMANIFEST hSignedDigestManifest;
        vrc = RTManifestCreate(0 /*fFlags*/, &hSignedDigestManifest);
        if (RT_SUCCESS(vrc))
        {
            RTVFSIOSTREAM hVfsIosTmp;
            vrc = RTVfsIoStrmFromBuffer(RTFILE_O_READ, pvSignature, (size_t)(pszSplit - (char *)pvSignature), &hVfsIosTmp);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTManifestReadStandardEx(hSignedDigestManifest, hVfsIosTmp, StaticErrInfo.szMsg, sizeof(StaticErrInfo.szMsg));
                RTVfsIoStrmRelease(hVfsIosTmp);
                if (RT_SUCCESS(vrc))
                {
                    /*
                     * Get signed digest, we prefer SHA-2, so explicitly query those first.
                     */
                    uint32_t fDigestType;
                    char     szSignedDigest[_8K + 1];
                    vrc = RTManifestEntryQueryAttr(hSignedDigestManifest, strManifestName.c_str(), NULL,
                                                   RTMANIFEST_ATTR_SHA512 | RTMANIFEST_ATTR_SHA256,
                                                   szSignedDigest, sizeof(szSignedDigest), &fDigestType);
                    if (vrc == VERR_MANIFEST_ATTR_TYPE_NOT_FOUND)
                        vrc = RTManifestEntryQueryAttr(hSignedDigestManifest, strManifestName.c_str(), NULL,
                                                       RTMANIFEST_ATTR_ANY, szSignedDigest, sizeof(szSignedDigest), &fDigestType);
                    if (RT_SUCCESS(vrc))
                    {
                        const char *pszSignedDigest = RTStrStrip(szSignedDigest);
                        size_t      cbSignedDigest  = strlen(pszSignedDigest) / 2;
                        uint8_t     abSignedDigest[sizeof(szSignedDigest) / 2];
                        vrc = RTStrConvertHexBytes(szSignedDigest, abSignedDigest, cbSignedDigest, 0 /*fFlags*/);
                        if (RT_SUCCESS(vrc))
                        {
                            /*
                             * Convert it to RTDIGESTTYPE_XXX and save the binary value for later use.
                             */
                            switch (fDigestType)
                            {
                                case RTMANIFEST_ATTR_SHA1:      m->enmSignedDigestType = RTDIGESTTYPE_SHA1; break;
                                case RTMANIFEST_ATTR_SHA256:    m->enmSignedDigestType = RTDIGESTTYPE_SHA256; break;
                                case RTMANIFEST_ATTR_SHA512:    m->enmSignedDigestType = RTDIGESTTYPE_SHA512; break;
                                case RTMANIFEST_ATTR_MD5:       m->enmSignedDigestType = RTDIGESTTYPE_MD5; break;
                                default:    AssertFailed();     m->enmSignedDigestType = RTDIGESTTYPE_INVALID; break;
                            }
                            if (m->enmSignedDigestType != RTDIGESTTYPE_INVALID)
                            {
                                m->pbSignedDigest = (uint8_t *)RTMemDup(abSignedDigest, cbSignedDigest);
                                m->cbSignedDigest = cbSignedDigest;
                                hrc = S_OK;
                            }
                            else
                                hrc = setError(E_FAIL, tr("Unsupported signed digest type (%#x)"), fDigestType);
                        }
                        else
                            hrc = setErrorVrc(vrc, tr("Error reading signed manifest digest: %Rrc"), vrc);
                    }
                    else if (vrc == VERR_NOT_FOUND)
                        hrc = setErrorVrc(vrc, tr("Could not locate signed digest for '%s' in the cert-file for '%s'"),
                                          strManifestName.c_str(), pTask->locInfo.strPath.c_str());
                    else
                        hrc = setErrorVrc(vrc, tr("RTManifestEntryQueryAttr failed unexpectedly: %Rrc"), vrc);
                }
                else
                    hrc = setErrorVrc(vrc, tr("Error parsing the .cert-file for '%s': %s"),
                                      pTask->locInfo.strPath.c_str(), StaticErrInfo.szMsg);
            }
            else
                hrc = E_OUTOFMEMORY;
            RTManifestRelease(hSignedDigestManifest);
        }
        else
            hrc = E_OUTOFMEMORY;

        /*
         * Look for the additional for PKCS#7/CMS signature we produce when we sign stuff.
         */
        if (SUCCEEDED(hrc))
        {
            *pszSplit = chSaved;
            vrc = RTCrPkcs7_ReadFromBuffer(&m->ContentInfo, pvSignature, cbSignature, RTCRPKCS7_READ_F_PEM_ONLY,
                                           &g_RTAsn1DefaultAllocator, NULL /*pfCmsLabeled*/,
                                           RTErrInfoInitStatic(&StaticErrInfo), pszSubFileNm);
            if (RT_SUCCESS(vrc))
                m->fContentInfoLoaded = true;
            else if (vrc != VERR_NOT_FOUND)
                hrc = setErrorVrc(vrc, tr("Error reading the PKCS#7/CMS signature from '%s' for '%s' (%Rrc): %s"),
                                  pszSubFileNm, pTask->locInfo.strPath.c_str(), vrc, StaticErrInfo.Core.pszMsg);
        }
    }
    else if (vrc == VERR_NOT_FOUND || vrc == VERR_EOF)
        hrc = setErrorBoth(E_FAIL, vrc, tr("Malformed .cert-file for '%s': Signer's certificate not found (%Rrc)"),
                           pTask->locInfo.strPath.c_str(), vrc);
    else
        hrc = setErrorVrc(vrc, tr("Error reading the signer's certificate from '%s' for '%s' (%Rrc): %s"),
                          pszSubFileNm, pTask->locInfo.strPath.c_str(), vrc, StaticErrInfo.Core.pszMsg);

    RTVfsIoStrmReadAllFree(pvSignature, cbSignature);
    LogFlowFunc(("returns %Rhrc (%Rrc)\n", hrc, vrc));
    return hrc;
}


/**
 * Does tail processing after the files have been read in.
 *
 * @param   pTask               The read task.
 * @returns COM status.
 * @throws  Nothing!
 */
HRESULT Appliance::i_readTailProcessing(TaskOVF *pTask)
{
    /*
     * Parse and validate the signature file.
     *
     * The signature file nominally has two parts, manifest part and a PEM
     * encoded certificate.  The former contains an entry for the manifest file
     * with a digest that is encrypted with the certificate in the latter part.
     *
     * When an appliance is signed by VirtualBox, a PKCS#7/CMS signedData part
     * is added by default, supplying more info than the bits mandated by the
     * OVF specs.  We will validate both the signedData and the standard OVF
     * signature.  Another requirement is that the first signedData signer
     * uses the same certificate as the regular OVF signature, allowing us to
     * only do path building for the signedData with the additional info it
     * ships with.
     */
    if (m->pbSignedDigest)
    {
        /* Since we're validating the digest of the manifest, there have to be
           a manifest.  We cannot allow a the manifest to be missing.  */
        if (m->hMemFileTheirManifest == NIL_RTVFSFILE)
            return setError(VBOX_E_FILE_ERROR, tr("Found .cert-file but no .mf-file for '%s'"), pTask->locInfo.strPath.c_str());

        /*
         * Validate the signed digest.
         *
         * It's possible we should allow the user to ignore signature
         * mismatches, but for now it is a solid show stopper.
         */
        HRESULT hrc;
        RTERRINFOSTATIC StaticErrInfo;

        /* Calc the digest of the manifest using the algorithm found above. */
        RTCRDIGEST hDigest;
        int vrc = RTCrDigestCreateByType(&hDigest, m->enmSignedDigestType);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTCrDigestUpdateFromVfsFile(hDigest, m->hMemFileTheirManifest, true /*fRewindFile*/);
            if (RT_SUCCESS(vrc))
            {
                /* Compare the signed digest with the one we just calculated.  (This
                   API will do the verification twice, once using IPRT's own crypto
                   and once using OpenSSL.  Both must OK it for success.) */
                vrc = RTCrPkixPubKeyVerifySignedDigestByCertPubKeyInfo(&m->SignerCert.TbsCertificate.SubjectPublicKeyInfo,
                                                                       m->pbSignedDigest, m->cbSignedDigest, hDigest,
                                                                       RTErrInfoInitStatic(&StaticErrInfo));
                if (RT_SUCCESS(vrc))
                {
                    m->fSignatureValid = true;
                    hrc = S_OK;
                }
                else if (vrc == VERR_CR_PKIX_SIGNATURE_MISMATCH)
                    hrc = setErrorVrc(vrc, tr("The manifest signature does not match"));
                else
                    hrc = setErrorVrc(vrc,
                                      tr("Error validating the manifest signature (%Rrc, %s)"), vrc, StaticErrInfo.Core.pszMsg);
            }
            else
                hrc = setErrorVrc(vrc, tr("RTCrDigestUpdateFromVfsFile failed: %Rrc"), vrc);
            RTCrDigestRelease(hDigest);
        }
        else
            hrc = setErrorVrc(vrc, tr("RTCrDigestCreateByType failed: %Rrc"), vrc);

        /*
         * If we have a PKCS#7/CMS signature, validate it and check that the
         * certificate matches the first signerInfo entry.
         */
        HRESULT hrc2 = i_readTailProcessingSignedData(&StaticErrInfo);
        if (FAILED(hrc2) && SUCCEEDED(hrc))
            hrc = hrc2;

        /*
         * Validate the certificate.
         *
         * We don't fail here if we cannot validate the certificate, we postpone
         * that till the import stage, so that we can allow the user to ignore it.
         *
         * The certificate validity time is deliberately left as warnings as the
         * OVF specification does not provision for any timestamping of the
         * signature. This is course a security concern, but the whole signing
         * of OVFs is currently weirdly trusting (self signed * certs), so this
         * is the least of our current problems.
         *
         * While we try build and verify certificate paths properly, the
         * "neighbours" quietly ignores this and seems only to check the signature
         * and not whether the certificate is trusted.  Also, we don't currently
         * complain about self-signed certificates either (ditto "neighbours").
         * The OVF creator is also a bit restricted wrt to helping us build the
         * path as he cannot supply intermediate certificates.  Anyway, we issue
         * warnings (goes to /dev/null, am I right?) for self-signed certificates
         * and certificates we cannot build and verify a root path for.
         *
         * (The OVF sillibuggers should've used PKCS#7, CMS or something else
         * that's already been standardized instead of combining manifests with
         * certificate PEM files in some very restrictive manner!  I wonder if
         * we could add a PKCS#7 section to the .cert file in addition to the CERT
         * and manifest stuff dictated by the standard.  Would depend on how others
         * deal with it.)
         */
        Assert(!m->fCertificateValid);
        Assert(m->fCertificateMissingPath);
        Assert(!m->fCertificateValidTime);
        Assert(m->strCertError.isEmpty());
        Assert(m->fCertificateIsSelfSigned == RTCrX509Certificate_IsSelfSigned(&m->SignerCert));

        /* We'll always needs the trusted cert store. */
        hrc2 = S_OK;
        RTCRSTORE hTrustedCerts;
        vrc = RTCrStoreCreateSnapshotOfUserAndSystemTrustedCAsAndCerts(&hTrustedCerts, RTErrInfoInitStatic(&StaticErrInfo));
        if (RT_SUCCESS(vrc))
        {
            /* If we don't have a PKCS7/CMS signature or if it uses a different
               certificate, we try our best to validate the OVF certificate. */
            if (!m->fContentInfoOkay || !m->fContentInfoSameCert)
            {
                if (m->fCertificateIsSelfSigned)
                    hrc2 = i_readTailProcessingVerifySelfSignedOvfCert(pTask, hTrustedCerts, &StaticErrInfo);
                else
                    hrc2 = i_readTailProcessingVerifyIssuedOvfCert(pTask, hTrustedCerts, &StaticErrInfo);
            }

            /* If there is a PKCS7/CMS signature, we always verify its certificates. */
            if (m->fContentInfoOkay)
            {
                void  *pvData = NULL;
                size_t cbData = 0;
                HRESULT hrc3 = i_readTailProcessingGetManifestData(&pvData, &cbData);
                if (SUCCEEDED(hrc3))
                {
                    hrc3 = i_readTailProcessingVerifyContentInfoCerts(pvData, cbData, hTrustedCerts, &StaticErrInfo);
                    RTMemTmpFree(pvData);
                }
                if (FAILED(hrc3) && SUCCEEDED(hrc2))
                    hrc2 = hrc3;
            }
            RTCrStoreRelease(hTrustedCerts);
        }
        else
            hrc2 = setErrorBoth(E_FAIL, vrc,
                                tr("Failed to query trusted CAs and Certificates from the system and for the current user (%Rrc%RTeim)"),
                                vrc, &StaticErrInfo.Core);

        /* Merge statuses from signature and certificate validation, prefering the signature one. */
        if (SUCCEEDED(hrc) && FAILED(hrc2))
            hrc = hrc2;
        if (FAILED(hrc))
            return hrc;
    }

    /** @todo provide details about the signatory, signature, etc.  */
    if (m->fSignerCertLoaded)
    {
        /** @todo PKCS7/CMS certs too */
        m->ptrCertificateInfo.createObject();
        m->ptrCertificateInfo->initCertificate(&m->SignerCert,
                                               m->fCertificateValid && !m->fCertificateMissingPath,
                                               !m->fCertificateValidTime);
    }

    /*
     * If there is a manifest, check that the OVF digest matches up (if present).
     */

    NOREF(pTask);
    return S_OK;
}

/**
 * Reads hMemFileTheirManifest into a memory buffer so it can be passed to
 * RTCrPkcs7VerifySignedDataWithExternalData.
 *
 * Use RTMemTmpFree to free the memory.
 */
HRESULT Appliance::i_readTailProcessingGetManifestData(void **ppvData, size_t *pcbData)
{
    uint64_t cbData;
    int vrc = RTVfsFileQuerySize(m->hMemFileTheirManifest, &cbData);
    AssertRCReturn(vrc, setErrorVrc(vrc, "RTVfsFileQuerySize"));

    void *pvData = RTMemTmpAllocZ((size_t)cbData);
    AssertPtrReturn(pvData, E_OUTOFMEMORY);

    vrc = RTVfsFileReadAt(m->hMemFileTheirManifest, 0, pvData, (size_t)cbData, NULL);
    AssertRCReturnStmt(vrc, RTMemTmpFree(pvData), setErrorVrc(vrc, "RTVfsFileReadAt"));

    *pcbData = (size_t)cbData;
    *ppvData = pvData;
    return S_OK;
}

/**
 * Worker for i_readTailProcessing that validates the signedData.
 *
 * If we have a PKCS#7/CMS signature:
 *      - validate it
 *      - check that the OVF certificate matches the first signerInfo entry
 *      - verify the signature, but leave the certificate path validation for
 *        later.
 *
 * @param   pErrInfo    Static error info buffer (not for returning, just for
 *                      avoiding wasting stack).
 * @returns COM status.
 * @throws  Nothing!
 */
HRESULT Appliance::i_readTailProcessingSignedData(PRTERRINFOSTATIC pErrInfo)
{
    m->fContentInfoOkay           = false;
    m->fContentInfoSameCert       = false;
    m->fContentInfoValidSignature = false;

    if (!m->fContentInfoLoaded)
        return S_OK;

    /*
     * Validate it.
     */
    HRESULT hrc = S_OK;
    PCRTCRPKCS7SIGNEDDATA pSignedData = m->ContentInfo.u.pSignedData;
    if (!RTCrPkcs7ContentInfo_IsSignedData(&m->ContentInfo))
        i_addWarning(tr("Invalid PKCS#7/CMS type: %s, expected %s (signedData)"),
                     m->ContentInfo.ContentType.szObjId, RTCRPKCS7SIGNEDDATA_OID);
    else if (RTAsn1ObjId_CompareWithString(&pSignedData->ContentInfo.ContentType, RTCR_PKCS7_DATA_OID) != 0)
        i_addWarning(tr("Invalid PKCS#7/CMS inner type: %s, expected %s (data)"),
                     pSignedData->ContentInfo.ContentType.szObjId, RTCR_PKCS7_DATA_OID);
    else if (RTAsn1OctetString_IsPresent(&pSignedData->ContentInfo.Content))
        i_addWarning(tr("Invalid PKCS#7/CMS data: embedded (%u bytes), expected external","",
                        pSignedData->ContentInfo.Content.Asn1Core.cb),
                     pSignedData->ContentInfo.Content.Asn1Core.cb);
    else if (pSignedData->SignerInfos.cItems == 0)
        i_addWarning(tr("Invalid PKCS#7/CMS: No signers"));
    else
    {
        m->fContentInfoOkay = true;

        /*
         * Same certificate as the OVF signature?
         */
        PCRTCRPKCS7SIGNERINFO pSignerInfo = pSignedData->SignerInfos.papItems[0];
        if (   RTCrX509Name_Compare(&pSignerInfo->IssuerAndSerialNumber.Name, &m->SignerCert.TbsCertificate.Issuer) == 0
            && RTAsn1Integer_Compare(&pSignerInfo->IssuerAndSerialNumber.SerialNumber,
                                     &m->SignerCert.TbsCertificate.SerialNumber) == 0)
            m->fContentInfoSameCert = true;
        else
            i_addWarning(tr("Invalid PKCS#7/CMS: Using a different certificate"));

        /*
         * Then perform a validation of the signatures, but first without
         * validating the certificate trust paths yet.
         */
        RTCRSTORE hTrustedCerts = NIL_RTCRSTORE;
        int vrc = RTCrStoreCreateInMem(&hTrustedCerts, 1);
        AssertRCReturn(vrc, setErrorVrc(vrc, tr("RTCrStoreCreateInMem failed: %Rrc"), vrc));

        vrc = RTCrStoreCertAddX509(hTrustedCerts, 0, &m->SignerCert, RTErrInfoInitStatic(pErrInfo));
        if (RT_SUCCESS(vrc))
        {
            void  *pvData = NULL;
            size_t cbData = 0;
            hrc = i_readTailProcessingGetManifestData(&pvData, &cbData);
            if (SUCCEEDED(hrc))
            {
                RTTIMESPEC Now;
                vrc = RTCrPkcs7VerifySignedDataWithExternalData(&m->ContentInfo, RTCRPKCS7VERIFY_SD_F_TRUST_ALL_CERTS,
                                                                NIL_RTCRSTORE /*hAdditionalCerts*/, hTrustedCerts,
                                                                RTTimeNow(&Now), NULL /*pfnVerifyCert*/, NULL /*pvUser*/,
                                                                pvData, cbData, RTErrInfoInitStatic(pErrInfo));
                if (RT_SUCCESS(vrc))
                    m->fContentInfoValidSignature = true;
                else
                    i_addWarning(tr("Failed to validate PKCS#7/CMS signature: %Rrc%RTeim"), vrc, &pErrInfo->Core);
                RTMemTmpFree(pvData);
            }
        }
        else
            hrc = setErrorVrc(vrc, tr("RTCrStoreCertAddX509 failed: %Rrc%RTeim"), vrc, &pErrInfo->Core);
        RTCrStoreRelease(hTrustedCerts);
    }

    return hrc;
}


/**
 * Worker for i_readTailProcessing that verifies a self signed certificate when
 * no PKCS\#7/CMS signature using the same certificate is present.
 */
HRESULT Appliance::i_readTailProcessingVerifySelfSignedOvfCert(TaskOVF *pTask, RTCRSTORE hTrustedStore, PRTERRINFOSTATIC pErrInfo)
{
    /*
     * It's a self signed certificate.  We assume the frontend will
     * present this fact to the user and give a choice whether this
     * is acceptable.  But, first make sure it makes internal sense.
     */
    m->fCertificateMissingPath = true;
    PCRTCRCERTCTX pCertCtx = RTCrStoreCertByIssuerAndSerialNo(hTrustedStore, &m->SignerCert.TbsCertificate.Issuer,
                                                              &m->SignerCert.TbsCertificate.SerialNumber);
    if (pCertCtx)
    {
        if (pCertCtx->pCert && RTCrX509Certificate_Compare(pCertCtx->pCert, &m->SignerCert) == 0)
            m->fCertificateMissingPath = true;
        RTCrCertCtxRelease(pCertCtx);
    }

    int vrc = RTCrX509Certificate_VerifySignatureSelfSigned(&m->SignerCert, RTErrInfoInitStatic(pErrInfo));
    if (RT_SUCCESS(vrc))
    {
        m->fCertificateValid = true;

        /* Check whether the certificate is currently valid, just warn if not. */
        RTTIMESPEC Now;
        m->fCertificateValidTime = RTCrX509Validity_IsValidAtTimeSpec(&m->SignerCert.TbsCertificate.Validity, RTTimeNow(&Now));
        if (m->fCertificateValidTime)
        {
            m->fCertificateValidTime = true;
            i_addWarning(tr("A self signed certificate was used to sign '%s'"), pTask->locInfo.strPath.c_str());
        }
        else
            i_addWarning(tr("Self signed certificate used to sign '%s' is not currently valid"),
                         pTask->locInfo.strPath.c_str());
    }
    else
    {
        m->strCertError.printfNoThrow(tr("Verification of the self signed certificate failed (%Rrc%#RTeim)"),
                                      vrc, &pErrInfo->Core);
        i_addWarning(tr("Verification of the self signed certificate used to sign '%s' failed (%Rrc)%RTeim"),
                     pTask->locInfo.strPath.c_str(), vrc, &pErrInfo->Core);
    }

    /* Just warn if it's not a CA. Self-signed certificates are
       hardly trustworthy to start with without the user's consent. */
    if (   !m->SignerCert.TbsCertificate.T3.pBasicConstraints
        || !m->SignerCert.TbsCertificate.T3.pBasicConstraints->CA.fValue)
        i_addWarning(tr("Self signed certificate used to sign '%s' is not marked as certificate authority (CA)"),
                     pTask->locInfo.strPath.c_str());

    return S_OK;
}

/**
 * Worker for i_readTailProcessing that verfies a non-self-issued OVF
 * certificate when no PKCS\#7/CMS signature using the same certificate is
 * present.
 */
HRESULT Appliance::i_readTailProcessingVerifyIssuedOvfCert(TaskOVF *pTask, RTCRSTORE hTrustedStore, PRTERRINFOSTATIC pErrInfo)
{
    /*
     * The certificate is not self-signed.  Use the system certificate
     * stores to try build a path that validates successfully.
     */
    HRESULT hrc = S_OK;
    RTCRX509CERTPATHS hCertPaths;
    int vrc = RTCrX509CertPathsCreate(&hCertPaths, &m->SignerCert);
    if (RT_SUCCESS(vrc))
    {
        /* Get trusted certificates from the system and add them to the path finding mission. */
        vrc = RTCrX509CertPathsSetTrustedStore(hCertPaths, hTrustedStore);
        if (RT_FAILURE(vrc))
            hrc = setErrorBoth(E_FAIL, vrc, tr("RTCrX509CertPathsSetTrustedStore failed (%Rrc)"), vrc);

        /* Add untrusted intermediate certificates. */
        if (RT_SUCCESS(vrc))
        {
            /// @todo RTCrX509CertPathsSetUntrustedStore(hCertPaths, hAdditionalCerts);
            /// We should look for intermediate certificates on the system, at least.
        }
        if (RT_SUCCESS(vrc))
        {
            /*
             * Do the building and verification of certificate paths.
             */
            vrc = RTCrX509CertPathsBuild(hCertPaths, RTErrInfoInitStatic(pErrInfo));
            if (RT_SUCCESS(vrc))
            {
                vrc = RTCrX509CertPathsValidateAll(hCertPaths, NULL, RTErrInfoInitStatic(pErrInfo));
                if (RT_SUCCESS(vrc))
                {
                    /*
                     * Mark the certificate as good.
                     */
                    /** @todo check the certificate purpose? If so, share with self-signed. */
                    m->fCertificateValid = true;
                    m->fCertificateMissingPath = false;

                    /*
                     * We add a warning if the certificate path isn't valid at the current
                     * time.  Since the time is only considered during path validation and we
                     * can repeat the validation process (but not building), it's easy to check.
                     */
                    RTTIMESPEC Now;
                    vrc = RTCrX509CertPathsSetValidTimeSpec(hCertPaths, RTTimeNow(&Now));
                    if (RT_SUCCESS(vrc))
                    {
                        vrc = RTCrX509CertPathsValidateAll(hCertPaths, NULL, RTErrInfoInitStatic(pErrInfo));
                        if (RT_SUCCESS(vrc))
                            m->fCertificateValidTime = true;
                        else
                            i_addWarning(tr("The certificate used to sign '%s' (or a certificate in the path) is not currently valid (%Rrc)"),
                                         pTask->locInfo.strPath.c_str(), vrc);
                    }
                    else
                        hrc = setErrorVrc(vrc, tr("RTCrX509CertPathsSetValidTimeSpec failed: %Rrc"), vrc);
                }
                else if (vrc == VERR_CR_X509_CPV_NO_TRUSTED_PATHS)
                {
                    m->fCertificateValid = true;
                    i_addWarning(tr("No trusted certificate paths"));

                    /* Add another warning if the pathless certificate is not valid at present. */
                    RTTIMESPEC Now;
                    if (RTCrX509Validity_IsValidAtTimeSpec(&m->SignerCert.TbsCertificate.Validity, RTTimeNow(&Now)))
                        m->fCertificateValidTime = true;
                    else
                        i_addWarning(tr("The certificate used to sign '%s' is not currently valid"),
                                     pTask->locInfo.strPath.c_str());
                }
                else
                    hrc = setErrorBoth(E_FAIL, vrc, tr("Certificate path validation failed (%Rrc%RTeim)"), vrc, &pErrInfo->Core);
            }
            else
                hrc = setErrorBoth(E_FAIL, vrc, tr("Certificate path building failed (%Rrc%RTeim)"), vrc, &pErrInfo->Core);
        }
        RTCrX509CertPathsRelease(hCertPaths);
    }
    else
        hrc = setErrorVrc(vrc, tr("RTCrX509CertPathsCreate failed: %Rrc"), vrc);
    return hrc;
}

/**
 * Helper for i_readTailProcessingVerifySignerInfo that reports a verfication
 * failure.
 *
 * @returns S_OK
 */
HRESULT Appliance::i_readTailProcessingVerifyContentInfoFailOne(const char *pszSignature, int vrc, PRTERRINFOSTATIC pErrInfo)
{
    i_addWarning(tr("%s verification failed: %Rrc%RTeim"), pszSignature, vrc, &pErrInfo->Core);
    if (m->strCertError.isEmpty())
        m->strCertError.printfNoThrow(tr("%s verification failed: %Rrc%RTeim"), pszSignature, vrc, &pErrInfo->Core);
    return S_OK;
}

/**
 * Worker for i_readTailProcessingVerifyContentInfoCerts that analyzes why the
 * standard verification of a signer info entry failed (@a vrc & @a pErrInfo).
 *
 * There are a couple of things we might want try to investigate deeper here:
 *      1. Untrusted signing certificate, often self-signed.
 *      2. Untrusted timstamp signing certificate.
 *      3. Certificate not valid at the current time and there isn't a
 *         timestamp counter signature.
 *
 * That said, it is difficult to get an accurate fix and report on the
 * issues here since there are a number of error sources, so just try identify
 * the more typical cases.
 *
 * @note Caller cleans up *phTrustedStore2 if not NIL.
 */
HRESULT Appliance::i_readTailProcessingVerifyAnalyzeSignerInfo(void const *pvData, size_t cbData, RTCRSTORE hTrustedStore,
                                                               uint32_t iSigner, PRTTIMESPEC pNow, int vrc,
                                                               PRTERRINFOSTATIC pErrInfo, PRTCRSTORE phTrustedStore2)
{
    PRTCRPKCS7SIGNEDDATA const pSignedData = m->ContentInfo.u.pSignedData;
    PRTCRPKCS7SIGNERINFO const pSigner     = pSignedData->SignerInfos.papItems[iSigner];

    /*
     * Error/warning message prefix:
     */
    const char *pszSignature;
    if (iSigner == 0 && m->fContentInfoSameCert)
        pszSignature = tr("OVF & PKCS#7/CMS signature");
    else
        pszSignature = tr("PKCS#7/CMS signature");
    char szSignatureBuf[64];
    if (pSignedData->SignerInfos.cItems > 1)
    {
        RTStrPrintf(szSignatureBuf, sizeof(szSignatureBuf), "%s #%u", pszSignature, iSigner + 1);
        pszSignature = szSignatureBuf;
    }

    /*
     * Don't try handle weird stuff:
     */
    /** @todo Are there more statuses we can deal with here? */
    if (   vrc != VERR_CR_X509_CPV_NOT_VALID_AT_TIME
        && vrc != VERR_CR_X509_NO_TRUST_ANCHOR)
        return i_readTailProcessingVerifyContentInfoFailOne(pszSignature, vrc, pErrInfo);

    /*
     * Find the signing certificate.
     * We require the certificate to be included in the signed data here.
     */
    PCRTCRX509CERTIFICATE pSigningCert;
    pSigningCert = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(&pSignedData->Certificates,
                                                                       &pSigner->IssuerAndSerialNumber.Name,
                                                                       &pSigner->IssuerAndSerialNumber.SerialNumber);
    if (!pSigningCert)
    {
        i_addWarning(tr("PKCS#7/CMS signature #%u does not include the signing certificate"), iSigner + 1);
        if (m->strCertError.isEmpty())
            m->strCertError.printfNoThrow(tr("PKCS#7/CMS signature #%u does not include the signing certificate"), iSigner + 1);
        return S_OK;
    }

    PCRTCRCERTCTX const pCertCtxTrusted = RTCrStoreCertByIssuerAndSerialNo(hTrustedStore, &pSigner->IssuerAndSerialNumber.Name,
                                                                           &pSigner->IssuerAndSerialNumber.SerialNumber);
    bool const          fSelfSigned     = RTCrX509Certificate_IsSelfSigned(pSigningCert);

    /*
     * Add warning about untrusted self-signed certificate:
     */
    if (fSelfSigned && !pCertCtxTrusted)
        i_addWarning(tr("%s: Untrusted self-signed certificate"), pszSignature);

    /*
     * Start by eliminating signing time issues (2 + 3) first as primary problem.
     * Keep the error info and status for later failures.
     */
    char szTime[RTTIME_STR_LEN];
    RTTIMESPEC Now2 = *pNow;
    vrc = RTCrPkcs7VerifySignedDataWithExternalData(&m->ContentInfo, RTCRPKCS7VERIFY_SD_F_USE_SIGNING_TIME_UNVERIFIED
                                                    | RTCRPKCS7VERIFY_SD_F_UPDATE_VALIDATION_TIME
                                                    | RTCRPKCS7VERIFY_SD_F_SIGNER_INDEX(iSigner)
                                                    | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS, NIL_RTCRSTORE,
                                                    hTrustedStore, &Now2, NULL, NULL,
                                                    pvData, cbData, RTErrInfoInitStatic(pErrInfo));
    if (RT_SUCCESS(vrc))
    {
        /* Okay, is it an untrusted time signing certificate or just signing time in general? */
        RTTIMESPEC Now3 = *pNow;
        vrc = RTCrPkcs7VerifySignedDataWithExternalData(&m->ContentInfo, RTCRPKCS7VERIFY_SD_F_USE_SIGNING_TIME_UNVERIFIED
                                                        | RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                        | RTCRPKCS7VERIFY_SD_F_UPDATE_VALIDATION_TIME
                                                        | RTCRPKCS7VERIFY_SD_F_SIGNER_INDEX(iSigner)
                                                        | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS, NIL_RTCRSTORE,
                                                        hTrustedStore, &Now3, NULL, NULL, pvData, cbData, NULL);
        if (RT_SUCCESS(vrc))
            i_addWarning(tr("%s: Untrusted timestamp (%s)"), pszSignature, RTTimeSpecToString(&Now3, szTime, sizeof(szTime)));
        else
            i_addWarning(tr("%s: Not valid at current time, but validates fine for untrusted signing time (%s)"),
                         pszSignature, RTTimeSpecToString(&Now2, szTime, sizeof(szTime)));
        return S_OK;
    }

    /* If we've got a trusted signing certificate (unlikely, but whatever), we can stop already.
       If we haven't got a self-signed certificate, stop too as messaging becomes complicated otherwise. */
    if (pCertCtxTrusted || !fSelfSigned)
        return i_readTailProcessingVerifyContentInfoFailOne(pszSignature, vrc, pErrInfo);

    int const vrcErrInfo = vrc;

    /*
     * Create a new trust store that includes the signing certificate
     * to see what that changes.
     */
    vrc = RTCrStoreCreateInMemEx(phTrustedStore2, 1, hTrustedStore);
    AssertRCReturn(vrc, setErrorVrc(vrc, "RTCrStoreCreateInMemEx"));
    vrc = RTCrStoreCertAddX509(*phTrustedStore2, 0, (PRTCRX509CERTIFICATE)pSigningCert, NULL);
    AssertRCReturn(vrc, setErrorVrc(vrc, "RTCrStoreCertAddX509/%u", iSigner));

    vrc = RTCrPkcs7VerifySignedDataWithExternalData(&m->ContentInfo,
                                                    RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                    | RTCRPKCS7VERIFY_SD_F_SIGNER_INDEX(iSigner)
                                                    | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS, NIL_RTCRSTORE,
                                                    *phTrustedStore2, pNow, NULL, NULL, pvData, cbData, NULL);
    if (RT_SUCCESS(vrc))
    {
        if (!fSelfSigned)
            i_readTailProcessingVerifyContentInfoFailOne(pszSignature, vrcErrInfo, pErrInfo);
        return S_OK;
    }

    /*
     * Time problems too?  Repeat what we did above, but with the modified trust store.
     */
    Now2 = *pNow;
    vrc = RTCrPkcs7VerifySignedDataWithExternalData(&m->ContentInfo, RTCRPKCS7VERIFY_SD_F_USE_SIGNING_TIME_UNVERIFIED
                                                    | RTCRPKCS7VERIFY_SD_F_UPDATE_VALIDATION_TIME
                                                    | RTCRPKCS7VERIFY_SD_F_SIGNER_INDEX(iSigner)
                                                    | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS, NIL_RTCRSTORE,
                                                    *phTrustedStore2, pNow, NULL, NULL, pvData, cbData, NULL);
    if (RT_SUCCESS(vrc))
    {
        /* Okay, is it an untrusted time signing certificate or just signing time in general? */
        RTTIMESPEC Now3 = *pNow;
        vrc = RTCrPkcs7VerifySignedDataWithExternalData(&m->ContentInfo, RTCRPKCS7VERIFY_SD_F_USE_SIGNING_TIME_UNVERIFIED
                                                        | RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                        | RTCRPKCS7VERIFY_SD_F_UPDATE_VALIDATION_TIME
                                                        | RTCRPKCS7VERIFY_SD_F_SIGNER_INDEX(iSigner)
                                                        | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS, NIL_RTCRSTORE,
                                                        *phTrustedStore2, &Now3, NULL, NULL, pvData, cbData, NULL);
        if (RT_SUCCESS(vrc))
            i_addWarning(tr("%s: Untrusted timestamp (%s)"), pszSignature, RTTimeSpecToString(&Now3, szTime, sizeof(szTime)));
        else
            i_addWarning(tr("%s: Not valid at current time, but validates fine for untrusted signing time (%s)"),
                         pszSignature, RTTimeSpecToString(&Now2, szTime, sizeof(szTime)));
    }
    else
        i_readTailProcessingVerifyContentInfoFailOne(pszSignature, vrcErrInfo, pErrInfo);

    return S_OK;
}

/**
 * Verify the signing certificates used to sign the PKCS\#7/CMS signature.
 *
 * ASSUMES that we've previously verified the PKCS\#7/CMS stuff in
 * trust-all-certs-without-question mode and it's just the certificate
 * validation that can fail now.
 */
HRESULT Appliance::i_readTailProcessingVerifyContentInfoCerts(void const *pvData, size_t cbData,
                                                              RTCRSTORE hTrustedStore, PRTERRINFOSTATIC pErrInfo)
{
    /*
     * Just do a run and see what happens (note we've already verified
     * the data signatures, which just leaves certificates and paths).
     */
    RTTIMESPEC Now;
    int vrc = RTCrPkcs7VerifySignedDataWithExternalData(&m->ContentInfo,
                                                          RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                        | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS,
                                                        NIL_RTCRSTORE /*hAdditionalCerts*/, hTrustedStore,
                                                        RTTimeNow(&Now), NULL /*pfnVerifyCert*/, NULL /*pvUser*/,
                                                        pvData, cbData, RTErrInfoInitStatic(pErrInfo));
    if (RT_SUCCESS(vrc))
        m->fContentInfoVerifiedOkay = true;
    else
    {
        /*
         * Deal with each of the signatures separately to try figure out
         * more exactly what's going wrong.
         */
        uint32_t             cVerifiedOkay = 0;
        PRTCRPKCS7SIGNEDDATA pSignedData   = m->ContentInfo.u.pSignedData;
        for (uint32_t iSigner = 0; iSigner < pSignedData->SignerInfos.cItems; iSigner++)
        {
            vrc = RTCrPkcs7VerifySignedDataWithExternalData(&m->ContentInfo,
                                                            RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                            | RTCRPKCS7VERIFY_SD_F_SIGNER_INDEX(iSigner)
                                                            | RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS,
                                                            NIL_RTCRSTORE /*hAdditionalCerts*/, hTrustedStore,
                                                            &Now, NULL /*pfnVerifyCert*/, NULL /*pvUser*/,
                                                            pvData, cbData, RTErrInfoInitStatic(pErrInfo));
            if (RT_SUCCESS(vrc))
                cVerifiedOkay++;
            else
            {
                RTCRSTORE hTrustedStore2 = NIL_RTCRSTORE;
                HRESULT hrc = i_readTailProcessingVerifyAnalyzeSignerInfo(pvData, cbData, hTrustedStore, iSigner, &Now,
                                                                          vrc, pErrInfo, &hTrustedStore2);
                RTCrStoreRelease(hTrustedStore2);
                if (FAILED(hrc))
                    return hrc;
            }
        }

        if (   pSignedData->SignerInfos.cItems > 1
            && pSignedData->SignerInfos.cItems != cVerifiedOkay)
            i_addWarning(tr("%u out of %u PKCS#7/CMS signatures verfified okay", "",
                            pSignedData->SignerInfos.cItems),
                         cVerifiedOkay, pSignedData->SignerInfos.cItems);
    }

    return S_OK;
}



/*******************************************************************************
 * Import stuff
 ******************************************************************************/

/**
 * Implementation for importing OVF data into VirtualBox. This starts a new thread which will call
 * Appliance::taskThreadImportOrExport().
 *
 * This creates one or more new machines according to the VirtualSystemScription instances created by
 * Appliance::Interpret().
 *
 * This is in a separate private method because it is used from one location:
 *
 * 1) from the public Appliance::ImportMachines().
 *
 * @param locInfo
 * @param progress
 * @return
 */
HRESULT Appliance::i_importImpl(const LocationInfo &locInfo,
                                ComObjPtr<Progress> &progress)
{
    HRESULT hrc;

    /* Initialize our worker task */
    ThreadTask *pTask;
    if (locInfo.storageType != VFSType_Cloud)
    {
        hrc = i_setUpProgress(progress, Utf8StrFmt(tr("Importing appliance '%s'"), locInfo.strPath.c_str()),
                              locInfo.storageType == VFSType_File ? ImportFile : ImportS3);
        if (FAILED(hrc))
            return setError(hrc, tr("Failed to create task for importing appliance into VirtualBox"));
        try
        {
            pTask = new TaskOVF(this, TaskOVF::Import, locInfo, progress);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }
    else
    {
        if (locInfo.strProvider.equals("OCI"))
        {
            /*
             * 1. Create a custom image from the instance:
             *    - 2 operations (starting and waiting)
             * 2. Import the custom image into the Object Storage (OCI format - TAR file with QCOW2 image and JSON file):
             *    - 2 operations (starting and waiting)
             * 3. Download the object from the Object Storage:
             *    - 1 operation (starting and downloadind is one operation)
             * 4. Open the object, extract an image and convert one to VDI:
             *    - 1 operation (extracting and conversion are piped) because only 1 base bootable image is imported for now
             * 5. Create VM with user settings and attach the converted image to VM:
             *    - 1 operation.
             * 6. Cleanup phase.
             *    - 1 to N operations.
             *    The number of the correct Progress operations are much tricky here.
             *    Whether Machine::deleteConfig() is called or Medium::deleteStorage() is called in the loop.
             *    Both require a new Progress object. To work with these functions the original Progress object uses
             *    the function Progress::waitForOtherProgressCompletion().
             *
             * Some speculation here...
             * Total: 2+2+1(cloud) + 1+1(local) + 1+1+1(cleanup) = 10 operations
             * or
             * Total: 2+2+1(cloud) + 1+1(local) + 1(cleanup) = 8 operations
             * if VM wasn't created we would have only 1 registered image for cleanup.
             *
             * Weight "#define"s for the Cloud operations are located in the file OCICloudClient.h.
             * Weight of cloud import operations (1-3 items from above):
             * Total = 750 = 25+75(start and wait)+25+375(start and wait)+250(download)
             *
             * Weight of local import operations (4-5 items from above):
             * Total = 150 = 100 (extract and convert) + 50 (create VM, attach disks)
             *
             * Weight of local cleanup operations (6 item from above):
             * Some speculation here...
             * Total = 3 = 1 (1 image) + 1 (1 setting file)+ 1 (1 prev setting file) - quick operations
             * or
             * Total = 1 (1 image) if VM wasn't created we would have only 1 registered image for now.
             */
            try
            {
                hrc = progress.createObject();
                if (SUCCEEDED(hrc))
                    hrc = progress->init(mVirtualBox, static_cast<IAppliance *>(this),
                                         Utf8Str(tr("Importing VM from Cloud...")),
                                         TRUE /* aCancelable */,
                                         10, // ULONG cOperations,
                                         1000, // ULONG ulTotalOperationsWeight,
                                         Utf8Str(tr("Start import VM from the Cloud...")), // aFirstOperationDescription
                                         25); // ULONG ulFirstOperationWeight
                if (SUCCEEDED(hrc))
                    pTask = new TaskCloud(this, TaskCloud::Import, locInfo, progress);
                else
                    pTask = NULL; /* shut up vcc */
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }
            if (FAILED(hrc))
                return setError(hrc, tr("Failed to create task for importing appliance into VirtualBox"));
        }
        else
            return setError(E_NOTIMPL, tr("Only \"OCI\" cloud provider is supported for now. \"%s\" isn't supported."),
                            locInfo.strProvider.c_str());
    }

    /*
     * Start the task thread.
     */
    hrc = pTask->createThread();
    pTask = NULL;
    if (SUCCEEDED(hrc))
        return hrc;
    return setError(hrc, tr("Failed to start thread for importing appliance into VirtualBox"));
}

/**
 * Actual worker code for importing OVF data into VirtualBox.
 *
 * This is called from Appliance::taskThreadImportOrExport() and therefore runs
 * on the OVF import worker thread. This creates one or more new machines
 * according to the VirtualSystemScription instances created by
 * Appliance::Interpret().
 *
 * This runs in two contexts:
 *
 * 1) in a first worker thread; in that case, Appliance::ImportMachines() called
 *    Appliance::i_importImpl();
 *
 * 2) in a second worker thread; in that case, Appliance::ImportMachines()
 *    called Appliance::i_importImpl(), which called Appliance::i_importFSOVA(),
 *    which called Appliance::i_importImpl(), which then called this again.
 *
 * @param   pTask       The OVF task data.
 * @return  COM status code.
 */
HRESULT Appliance::i_importFS(TaskOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("Appliance %p\n", this));

    /* Change the appliance state so we can safely leave the lock while doing
     * time-consuming image imports; also the below method calls do all kinds of
     * locking which conflicts with the appliance object lock. */
    AutoWriteLock writeLock(this COMMA_LOCKVAL_SRC_POS);
    /* Check if the appliance is currently busy. */
    if (!i_isApplianceIdle())
        return E_ACCESSDENIED;
    /* Set the internal state to importing. */
    m->state = ApplianceImporting;

    HRESULT hrc = S_OK;

    /* Clear the list of imported machines, if any */
    m->llGuidsMachinesCreated.clear();

    if (pTask->locInfo.strPath.endsWith(".ovf", Utf8Str::CaseInsensitive))
        hrc = i_importFSOVF(pTask, writeLock);
    else
        hrc = i_importFSOVA(pTask, writeLock);
    if (FAILED(hrc))
    {
        /* With _whatever_ error we've had, do a complete roll-back of
         * machines and images we've created */
        writeLock.release();
        ErrorInfoKeeper eik;
        for (list<Guid>::iterator itID = m->llGuidsMachinesCreated.begin();
             itID != m->llGuidsMachinesCreated.end();
             ++itID)
        {
            Guid guid = *itID;
            Bstr bstrGuid = guid.toUtf16();
            ComPtr<IMachine> failedMachine;
            HRESULT hrc2 = mVirtualBox->FindMachine(bstrGuid.raw(), failedMachine.asOutParam());
            if (SUCCEEDED(hrc2))
            {
                SafeIfaceArray<IMedium> aMedia;
                hrc2 = failedMachine->Unregister(CleanupMode_DetachAllReturnHardDisksOnly, ComSafeArrayAsOutParam(aMedia));
                if (FAILED(hrc2))
                    LogRel(("Appliance::i_importFS: Cleaning up failed import failed with hrc2 -> %Rhrc in IMachine::Unregister()\n", hrc2));
                ComPtr<IProgress> pProgress2;
                hrc2 = failedMachine->DeleteConfig(ComSafeArrayAsInParam(aMedia), pProgress2.asOutParam());
                if (FAILED(hrc2))
                    LogRel(("Appliance::i_importFS: Cleaning up failed import failed with hrc2 -> %Rhrc in IMachine::DeleteConfig()\n", hrc2));
                pProgress2->WaitForCompletion(-1);
            }
        }
        writeLock.acquire();
    }

    /* Reset the state so others can call methods again */
    m->state = ApplianceIdle;

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    LogFlowFuncLeave();
    return hrc;
}

HRESULT Appliance::i_importFSOVF(TaskOVF *pTask, AutoWriteLockBase &rWriteLock)
{
    return i_importDoIt(pTask, rWriteLock);
}

HRESULT Appliance::i_importFSOVA(TaskOVF *pTask, AutoWriteLockBase &rWriteLock)
{
    LogFlowFuncEnter();

    /*
     * Open the tar file as file stream.
     */
    RTVFSIOSTREAM hVfsIosOva;
    int vrc = RTVfsIoStrmOpenNormal(pTask->locInfo.strPath.c_str(),
                                    RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN, &hVfsIosOva);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Error opening the OVA file '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

    RTVFSFSSTREAM hVfsFssOva;
    vrc = RTZipTarFsStreamFromIoStream(hVfsIosOva, 0 /*fFlags*/, &hVfsFssOva);
    RTVfsIoStrmRelease(hVfsIosOva);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("Error reading the OVA file '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

    /*
     * Join paths with the i_importFSOVF code.
     *
     * Note! We don't need to skip the OVF, manifest or signature files, as the
     *       i_importMachineGeneric, i_importVBoxMachine and i_importOpenSourceFile
     *       code will deal with this (as there could be other files in the OVA
     *       that we don't process, like 'de-DE-resources.xml' in EXAMPLE 1,
     *       Appendix D.1, OVF v2.1.0).
     */
    HRESULT hrc = i_importDoIt(pTask, rWriteLock, hVfsFssOva);

    RTVfsFsStrmRelease(hVfsFssOva);

    LogFlowFunc(("returns %Rhrc\n", hrc));
    return hrc;
}

/**
 * Does the actual importing after the caller has made the source accessible.
 *
 * @param   pTask               The import task.
 * @param   rWriteLock          The write lock the caller's caller is holding,
 *                              will be released for some reason.
 * @param   hVfsFssOva          The file system stream if OVA, NIL if not.
 * @returns COM status code.
 * @throws  Nothing.
 */
HRESULT Appliance::i_importDoIt(TaskOVF *pTask, AutoWriteLockBase &rWriteLock, RTVFSFSSTREAM hVfsFssOva /*= NIL_RTVFSFSSTREAM*/)
{
    rWriteLock.release();

    HRESULT hrc = E_FAIL;
    try
    {
        /*
         * Create the import stack for the rollback on errors.
         */
        ImportStack stack(pTask->locInfo, m->pReader->m_mapDisks, pTask->pProgress, hVfsFssOva);

        try
        {
            /* Do the importing. */
            i_importMachines(stack);

            /* We should've processed all the files now, so compare. */
            hrc = i_verifyManifestFile(stack);

            /* If everything was successful so far check if some extension
             * pack wants to do file sanity checking. */
            if (SUCCEEDED(hrc))
            {
                /** @todo */;
            }
        }
        catch (HRESULT hrcXcpt)
        {
            hrc = hrcXcpt;
        }
        catch (...)
        {
            AssertFailed();
            hrc = E_FAIL;
        }
        if (FAILED(hrc))
        {
            /*
             * Restoring original UUID from OVF description file.
             * During import VBox creates new UUIDs for imported images and
             * assigns them to the images. In case of failure we have to restore
             * the original UUIDs because those new UUIDs are obsolete now and
             * won't be used anymore.
             */
            ErrorInfoKeeper eik; /* paranoia */
            list< ComObjPtr<VirtualSystemDescription> >::const_iterator itvsd;
            /* Iterate through all virtual systems of that appliance */
            for (itvsd = m->virtualSystemDescriptions.begin();
                 itvsd != m->virtualSystemDescriptions.end();
                 ++itvsd)
            {
                ComObjPtr<VirtualSystemDescription> vsdescThis = (*itvsd);
                settings::MachineConfigFile *pConfig = vsdescThis->m->pConfig;
                if(vsdescThis->m->pConfig!=NULL)
                    stack.restoreOriginalUUIDOfAttachedDevice(pConfig);
            }
        }
    }
    catch (...)
    {
        hrc = E_FAIL;
        AssertFailed();
    }

    rWriteLock.acquire();
    return hrc;
}

/**
 * Undocumented, you figure it from the name.
 *
 * @returns Undocumented
 * @param   stack               Undocumented.
 */
HRESULT Appliance::i_verifyManifestFile(ImportStack &stack)
{
    LogFlowThisFuncEnter();
    HRESULT hrc;
    int vrc;

    /*
     * No manifest is fine, it always matches.
     */
    if (m->hTheirManifest == NIL_RTMANIFEST)
        hrc = S_OK;
    else
    {
        /*
         * Hack: If the manifest we just read doesn't have a digest for the OVF, copy
         *       it from the manifest we got from the caller.
         * @bugref{6022#c119}
         */
        if (   !RTManifestEntryExists(m->hTheirManifest, m->strOvfManifestEntry.c_str())
            && RTManifestEntryExists(m->hOurManifest, m->strOvfManifestEntry.c_str()) )
        {
            uint32_t fType = 0;
            char szDigest[512 + 1];
            vrc = RTManifestEntryQueryAttr(m->hOurManifest, m->strOvfManifestEntry.c_str(), NULL, RTMANIFEST_ATTR_ANY,
                                           szDigest, sizeof(szDigest), &fType);
            if (RT_SUCCESS(vrc))
                vrc = RTManifestEntrySetAttr(m->hTheirManifest, m->strOvfManifestEntry.c_str(),
                                             NULL /*pszAttr*/, szDigest, fType);
            if (RT_FAILURE(vrc))
                return setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Error fudging missing OVF digest in manifest: %Rrc"), vrc);
        }

        /*
         * Compare with the digests we've created while read/processing the import.
         *
         * We specify the RTMANIFEST_EQUALS_IGN_MISSING_ATTRS to ignore attributes
         * (SHA1, SHA256, etc) that are only present in one of the manifests, as long
         * as each entry has at least one common attribute that we can check.  This
         * is important for the OVF in OVAs, for which we generates several digests
         * since we don't know which are actually used in the manifest (OVF comes
         * first in an OVA, then manifest).
         */
        char szErr[256];
        vrc = RTManifestEqualsEx(m->hTheirManifest, m->hOurManifest, NULL /*papszIgnoreEntries*/,
                                 NULL /*papszIgnoreAttrs*/,
                                 RTMANIFEST_EQUALS_IGN_MISSING_ATTRS | RTMANIFEST_EQUALS_IGN_MISSING_ENTRIES_2ND,
                                 szErr, sizeof(szErr));
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = setErrorVrc(vrc, tr("Digest mismatch (%Rrc): %s"), vrc, szErr);
    }

    NOREF(stack);
    LogFlowThisFunc(("returns %Rhrc\n", hrc));
    return hrc;
}

/**
 * Helper that converts VirtualSystem attachment values into VirtualBox attachment values.
 * Throws HRESULT values on errors!
 *
 * @param hdc in: the HardDiskController structure to attach to.
 * @param ulAddressOnParent in: the AddressOnParent parameter from OVF.
 * @param controllerName out: the name of the storage controller to attach to (e.g. "IDE").
 * @param lControllerPort out: the channel (controller port) of the controller to attach to.
 * @param lDevice out: the device number to attach to.
 */
void Appliance::i_convertDiskAttachmentValues(const ovf::HardDiskController &hdc,
                                              uint32_t ulAddressOnParent,
                                              Utf8Str &controllerName,
                                              int32_t &lControllerPort,
                                              int32_t &lDevice)
{
    Log(("Appliance::i_convertDiskAttachmentValues: hdc.system=%d, hdc.fPrimary=%d, ulAddressOnParent=%d\n",
         hdc.system,
         hdc.fPrimary,
         ulAddressOnParent));

    switch (hdc.system)
    {
        case ovf::HardDiskController::IDE:
            // For the IDE bus, the port parameter can be either 0 or 1, to specify the primary
            // or secondary IDE controller, respectively. For the primary controller of the IDE bus,
            // the device number can be either 0 or 1, to specify the master or the slave device,
            // respectively. For the secondary IDE controller, the device number is always 1 because
            // the master device is reserved for the CD-ROM drive.
            controllerName = "IDE";
            switch (ulAddressOnParent)
            {
                case 0: // master
                    if (!hdc.fPrimary)
                    {
                        // secondary master
                        lControllerPort = 1;
                        lDevice         = 0;
                    }
                    else // primary master
                    {
                        lControllerPort = 0;
                        lDevice         = 0;
                    }
                    break;

                case 1: // slave
                    if (!hdc.fPrimary)
                    {
                        // secondary slave
                        lControllerPort = 1;
                        lDevice         = 1;
                    }
                    else // primary slave
                    {
                        lControllerPort = 0;
                        lDevice         = 1;
                    }
                    break;

                // used by older VBox exports
                case 2:     // interpret this as secondary master
                    lControllerPort = 1;
                    lDevice         = 0;
                    break;

                // used by older VBox exports
                case 3:     // interpret this as secondary slave
                    lControllerPort = 1;
                    lDevice         = 1;
                    break;

                default:
                    throw setError(VBOX_E_NOT_SUPPORTED,
                                   tr("Invalid channel %RU32 specified; IDE controllers support only 0, 1 or 2"),
                                   ulAddressOnParent);
                    break;
            }
            break;

        case ovf::HardDiskController::SATA:
            controllerName  = "SATA";
            lControllerPort = (int32_t)ulAddressOnParent;
            lDevice         = 0;
            break;

        case ovf::HardDiskController::SCSI:
        {
            if (hdc.strControllerType.compare("lsilogicsas")==0)
                controllerName = "SAS";
            else
                controllerName = "SCSI";
            lControllerPort = (int32_t)ulAddressOnParent;
            lDevice         = 0;
            break;
        }

        case ovf::HardDiskController::VIRTIOSCSI:
            controllerName  = "VirtioSCSI";
            lControllerPort = (int32_t)ulAddressOnParent;
            lDevice         = 0;
            break;

        case ovf::HardDiskController::NVMe:
            controllerName  = "NVMe";
            lControllerPort = (int32_t)ulAddressOnParent;
            lDevice         = 0;
            break;

        default: break;
    }

    Log(("=> lControllerPort=%d, lDevice=%d\n", lControllerPort, lDevice));
}

/**
 * Imports one image.
 *
 * This is common code shared between
 *  --  i_importMachineGeneric() for the OVF case; in that case the information comes from
 *      the OVF virtual systems;
 *  --  i_importVBoxMachine(); in that case, the information comes from the <vbox:Machine>
 *      tag.
 *
 * Both ways of describing machines use the OVF disk references section, so in both cases
 * the caller needs to pass in the ovf::DiskImage structure from ovfreader.cpp.
 *
 * As a result, in both cases, if di.strHref is empty, we create a new image as per the OVF
 * spec, even though this cannot really happen in the vbox:Machine case since such data
 * would never have been exported.
 *
 * This advances stack.pProgress by one operation with the image's weight.
 *
 * @param di ovfreader.cpp structure describing the image from the OVF that is to be imported
 * @param strDstPath Where to create the target image.
 * @param pTargetMedium out: The newly created target medium. This also gets pushed on stack.llHardDisksCreated for cleanup.
 * @param stack
 *
 * @throws HRESULT
 */
void Appliance::i_importOneDiskImage(const ovf::DiskImage &di,
                                     const Utf8Str &strDstPath,
                                     ComObjPtr<Medium> &pTargetMedium,
                                     ImportStack &stack)
{
    HRESULT hrc;

    Utf8Str strAbsDstPath;
    int vrc = RTPathAbsExCxx(strAbsDstPath, stack.strMachineFolder, strDstPath);
    AssertRCStmt(vrc, throw Global::vboxStatusCodeToCOM(vrc));

    /* Get the system properties. */
    SystemProperties *pSysProps = mVirtualBox->i_getSystemProperties();

    /* Keep the source file ref handy for later. */
    const Utf8Str &strSourceOVF = di.strHref;

    /* Construct source file path */
    Utf8Str strSrcFilePath;
    if (stack.hVfsFssOva != NIL_RTVFSFSSTREAM)
        strSrcFilePath = strSourceOVF;
    else
    {
        strSrcFilePath = stack.strSourceDir;
        strSrcFilePath.append(RTPATH_SLASH_STR);
        strSrcFilePath.append(strSourceOVF);
    }

    /* First of all check if the original (non-absolute) destination path is
     * a valid medium UUID. If so, the user wants to import the image into
     * an existing path. This is useful for iSCSI for example. */
    /** @todo r=klaus the code structure after this point is totally wrong,
     * full of unnecessary code duplication and other issues. 4.2 still had
     * the right structure for importing into existing medium objects, which
     * the current code can't possibly handle. */
    RTUUID uuid;
    vrc = RTUuidFromStr(&uuid, strDstPath.c_str());
    if (vrc == VINF_SUCCESS)
    {
        hrc = mVirtualBox->i_findHardDiskById(Guid(uuid), true, &pTargetMedium);
        if (FAILED(hrc)) throw hrc;
    }
    else
    {
        RTVFSIOSTREAM hVfsIosSrc = NIL_RTVFSIOSTREAM;

        /* check read file to GZIP compression */
        bool const fGzipped = di.strCompression.compare("gzip", Utf8Str::CaseInsensitive) == 0;
        Utf8Str strDeleteTemp;
        try
        {
            Utf8Str strTrgFormat = "VMDK";
            ComObjPtr<MediumFormat> trgFormat;
            Bstr bstrFormatName;
            ULONG lCabs = 0;

            char *pszSuff = RTPathSuffix(strAbsDstPath.c_str());
            if (pszSuff != NULL)
            {
                /*
                 * Figure out which format the user like to have. Default is VMDK
                 * or it can be VDI if according command-line option is set
                 */

                /*
                 * We need a proper target format
                 * if target format has been changed by user via GUI import wizard
                 * or via VBoxManage import command (option --importtovdi)
                 * then we need properly process such format like ISO
                 * Because there is no conversion ISO to VDI
                 */
                trgFormat = pSysProps->i_mediumFormatFromExtension(++pszSuff);
                if (trgFormat.isNull())
                    throw setError(E_FAIL, tr("Unsupported medium format for disk image '%s'"), di.strHref.c_str());

                hrc = trgFormat->COMGETTER(Name)(bstrFormatName.asOutParam());
                if (FAILED(hrc)) throw hrc;

                strTrgFormat = Utf8Str(bstrFormatName);

                if (   m->optListImport.contains(ImportOptions_ImportToVDI)
                    && strTrgFormat.compare("RAW", Utf8Str::CaseInsensitive) != 0)
                {
                    /* change the target extension */
                    strTrgFormat = "vdi";
                    trgFormat = pSysProps->i_mediumFormatFromExtension(strTrgFormat);
                    strAbsDstPath.stripSuffix();
                    strAbsDstPath.append(".");
                    strAbsDstPath.append(strTrgFormat.c_str());
                }

                /* Check the capabilities. We need create capabilities. */
                lCabs = 0;
                com::SafeArray <MediumFormatCapabilities_T> mediumFormatCap;
                hrc = trgFormat->COMGETTER(Capabilities)(ComSafeArrayAsOutParam(mediumFormatCap));

                if (FAILED(hrc))
                    throw hrc;

                for (ULONG j = 0; j < mediumFormatCap.size(); j++)
                    lCabs |= mediumFormatCap[j];

                if (   !(lCabs & MediumFormatCapabilities_CreateFixed)
                    && !(lCabs & MediumFormatCapabilities_CreateDynamic) )
                    throw setError(VBOX_E_NOT_SUPPORTED,
                                   tr("Could not find a valid medium format for the target disk '%s'"),
                                   strAbsDstPath.c_str());
            }
            else
            {
                throw setError(VBOX_E_FILE_ERROR,
                               tr("The target disk '%s' has no extension "),
                               strAbsDstPath.c_str(), VERR_INVALID_NAME);
            }

            /*CD/DVD case*/
            if (strTrgFormat.compare("RAW", Utf8Str::CaseInsensitive) == 0)
            {
                try
                {
                    if (fGzipped)
                        i_importDecompressFile(stack, strSrcFilePath, strAbsDstPath, strSourceOVF.c_str());
                    else
                        i_importCopyFile(stack, strSrcFilePath, strAbsDstPath, strSourceOVF.c_str());

                    ComPtr<IMedium> pTmp;
                    hrc = mVirtualBox->OpenMedium(Bstr(strAbsDstPath).raw(),
                                                  DeviceType_DVD,
                                                  AccessMode_ReadWrite,
                                                  false,
                                                  pTmp.asOutParam());
                    if (FAILED(hrc))
                        throw hrc;

                    IMedium *iM = pTmp;
                    pTargetMedium = static_cast<Medium*>(iM);
                }
                catch (HRESULT /*arc*/)
                {
                    throw;
                }

                /* Advance to the next operation. */
                /* operation's weight, as set up with the IProgress originally */
                stack.pProgress->SetNextOperation(BstrFmt(tr("Importing virtual disk image '%s'"),
                                                  RTPathFilename(strSourceOVF.c_str())).raw(),
                                                  di.ulSuggestedSizeMB);
            }
            else/* HDD case*/
            {
                /* Create an IMedium object. */
                pTargetMedium.createObject();

                hrc = pTargetMedium->init(mVirtualBox,
                                          strTrgFormat,
                                          strAbsDstPath,
                                          Guid::Empty /* media registry: none yet */,
                                          DeviceType_HardDisk);
                if (FAILED(hrc)) throw hrc;

                ComPtr<IProgress> pProgressImport;
                /* If strHref is empty we have to create a new file. */
                if (strSourceOVF.isEmpty())
                {
                    com::SafeArray<MediumVariant_T>  mediumVariant;
                    mediumVariant.push_back(MediumVariant_Standard);

                    /* Kick off the creation of a dynamic growing disk image with the given capacity. */
                    hrc = pTargetMedium->CreateBaseStorage(di.iCapacity / _1M,
                                                           ComSafeArrayAsInParam(mediumVariant),
                                                           pProgressImport.asOutParam());
                    if (FAILED(hrc)) throw hrc;

                    /* Advance to the next operation. */
                    /* operation's weight, as set up with the IProgress originally */
                    stack.pProgress->SetNextOperation(BstrFmt(tr("Creating disk image '%s'"),
                                                      strAbsDstPath.c_str()).raw(),
                                                      di.ulSuggestedSizeMB);
                }
                else
                {
                    /* We need a proper source format description */
                    /* Which format to use? */
                    ComObjPtr<MediumFormat> srcFormat;
                    hrc = i_findMediumFormatFromDiskImage(di, srcFormat);
                    if (FAILED(hrc))
                        throw setError(VBOX_E_NOT_SUPPORTED,
                                       tr("Could not find a valid medium format for the source disk '%s' "
                                          "Check correctness of the image format URL in the OVF description file "
                                          "or extension of the image"),
                                       RTPathFilename(strSourceOVF.c_str()));

                    /* If gzipped, decompress the GZIP file and save a new file in the target path */
                    if (fGzipped)
                    {
                        Utf8Str strTargetFilePath(strAbsDstPath);
                        strTargetFilePath.stripFilename();
                        strTargetFilePath.append(RTPATH_SLASH_STR);
                        strTargetFilePath.append("temp_");
                        strTargetFilePath.append(RTPathFilename(strSrcFilePath.c_str()));
                        strDeleteTemp = strTargetFilePath;

                        i_importDecompressFile(stack, strSrcFilePath, strTargetFilePath, strSourceOVF.c_str());

                        /* Correct the source and the target with the actual values */
                        strSrcFilePath = strTargetFilePath;

                        /* Open the new source file. */
                        vrc = RTVfsIoStrmOpenNormal(strSrcFilePath.c_str(), RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                                    &hVfsIosSrc);
                        if (RT_FAILURE(vrc))
                            throw setErrorVrc(vrc, tr("Error opening decompressed image file '%s' (%Rrc)"),
                                              strSrcFilePath.c_str(), vrc);
                    }
                    else
                        hVfsIosSrc = i_importOpenSourceFile(stack, strSrcFilePath, strSourceOVF.c_str());

                    /* Add a read ahead thread to try speed things up with concurrent reads and
                       writes going on in different threads. */
                    RTVFSIOSTREAM hVfsIosReadAhead;
                    vrc = RTVfsCreateReadAheadForIoStream(hVfsIosSrc, 0 /*fFlags*/, 0 /*cBuffers=default*/,
                                                          0 /*cbBuffers=default*/, &hVfsIosReadAhead);
                    RTVfsIoStrmRelease(hVfsIosSrc);
                    if (RT_FAILURE(vrc))
                        throw setErrorVrc(vrc, tr("Error initializing read ahead thread for '%s' (%Rrc)"),
                                          strSrcFilePath.c_str(), vrc);

                    /* Start the source image cloning operation. */
                    ComObjPtr<Medium> nullParent;
                    ComObjPtr<Progress> pProgressImportTmp;
                    hrc = pProgressImportTmp.createObject();
                    if (FAILED(hrc)) throw hrc;
                    hrc = pProgressImportTmp->init(mVirtualBox,
                                                   static_cast<IAppliance*>(this),
                                                   Utf8StrFmt(tr("Importing medium '%s'"), strAbsDstPath.c_str()),
                                                   TRUE);
                    if (FAILED(hrc)) throw hrc;
                    pProgressImportTmp.queryInterfaceTo(pProgressImport.asOutParam());
                    /* pProgressImportTmp is in parameter for Medium::i_importFile,
                     * which is somewhat unusual and might be changed later. */
                    hrc = pTargetMedium->i_importFile(strSrcFilePath.c_str(),
                                                      srcFormat,
                                                      MediumVariant_Standard,
                                                      hVfsIosReadAhead,
                                                      nullParent,
                                                      pProgressImportTmp,
                                                      true /* aNotify */);
                    RTVfsIoStrmRelease(hVfsIosReadAhead);
                    hVfsIosSrc = NIL_RTVFSIOSTREAM;
                    if (FAILED(hrc))
                        throw hrc;

                    /* Advance to the next operation. */
                    /* operation's weight, as set up with the IProgress originally */
                    stack.pProgress->SetNextOperation(BstrFmt(tr("Importing virtual disk image '%s'"),
                                                      RTPathFilename(strSourceOVF.c_str())).raw(),
                                                      di.ulSuggestedSizeMB);
                }

                /* Now wait for the background import operation to complete; this throws
                 * HRESULTs on error. */
                stack.pProgress->WaitForOtherProgressCompletion(pProgressImport, 0 /* indefinite wait */);

                /* The creating/importing has placed the medium in the global
                 * media registry since the VM isn't created yet. Remove it
                 * again to let it added to the right registry when the VM
                 * has been created below. */
                pTargetMedium->i_removeRegistry(mVirtualBox->i_getGlobalRegistryId());
            }
        }
        catch (...)
        {
            if (strDeleteTemp.isNotEmpty())
                RTFileDelete(strDeleteTemp.c_str());
            throw;
        }

        /* Make sure the source file is closed. */
        if (hVfsIosSrc != NIL_RTVFSIOSTREAM)
            RTVfsIoStrmRelease(hVfsIosSrc);

        /*
         * Delete the temp gunzip result, if any.
         */
        if (strDeleteTemp.isNotEmpty())
        {
            vrc = RTFileDelete(strSrcFilePath.c_str());
            if (RT_FAILURE(vrc))
                setWarning(VBOX_E_FILE_ERROR,
                           tr("Failed to delete the temporary file '%s' (%Rrc)"), strSrcFilePath.c_str(), vrc);
        }
    }
}

/**
 * Helper routine to parse the ExtraData Utf8Str for a storage controller's
 * value or channel value.
 *
 * @param   aExtraData    The ExtraData string with a format of
 *                        'controller=13;channel=3'.
 * @param   pszKey        The string being looked up, either 'controller' or
 *                        'channel'.
 * @param   puVal         The integer value of the 'controller=' or 'channel='
 *                        key in the ExtraData string.
 * @returns COM status code.
 * @throws  Nothing.
 */
static int getStorageControllerDetailsFromStr(const com::Utf8Str &aExtraData, const char *pszKey, uint32_t *puVal)
{
    size_t posKey = aExtraData.find(pszKey);
    if (posKey == Utf8Str::npos)
        return VERR_INVALID_PARAMETER;

    int vrc = RTStrToUInt32Ex(aExtraData.c_str() + posKey + strlen(pszKey), NULL, 0, puVal);
    if (vrc == VWRN_NUMBER_TOO_BIG || vrc == VWRN_NEGATIVE_UNSIGNED)
        return VERR_INVALID_PARAMETER;

    return vrc;
}

/**
 * Verifies the validity of a storage controller's channel (aka controller port).
 *
 * @param   aStorageControllerType     The type of storage controller as idenfitied
 *                                     by the enum of type StorageControllerType_T.
 * @param   uControllerPort            The controller port value.
 * @param   aMaxPortCount              The maximum number of ports allowed for this
 *                                     storage controller type.
 * @returns COM status code.
 * @throws  Nothing.
 */
HRESULT Appliance::i_verifyStorageControllerPortValid(const StorageControllerType_T aStorageControllerType,
                                                      const uint32_t uControllerPort,
                                                      ULONG *aMaxPortCount)
{
    ComPtr<IPlatformProperties> platformProperties;
    mVirtualBox->GetPlatformProperties(PlatformArchitecture_x86, platformProperties.asOutParam()); /// @todo BUGBUG Only x86 for now!

    StorageBus_T enmStorageBus = StorageBus_Null;
    HRESULT hrc = platformProperties->GetStorageBusForControllerType(aStorageControllerType, &enmStorageBus);
    if (FAILED(hrc))
        return hrc;

    hrc = platformProperties->GetMaxPortCountForStorageBus(enmStorageBus, aMaxPortCount);
    if (FAILED(hrc))
        return hrc;

    if (uControllerPort >= *aMaxPortCount)
        return E_INVALIDARG;

    return S_OK;
}

/**
 * Imports one OVF virtual system (described by the given ovf::VirtualSystem and VirtualSystemDescription)
 * into VirtualBox by creating an IMachine instance, which is returned.
 *
 * This throws HRESULT error codes for anything that goes wrong, in which case the caller must clean
 * up any leftovers from this function. For this, the given ImportStack instance has received information
 * about what needs cleaning up (to support rollback).
 *
 * @param       vsysThis        OVF virtual system (machine) to import.
 * @param       vsdescThis      Matching virtual system description (machine) to import.
 * @param[out]  pNewMachineRet  Newly created machine.
 * @param       stack           Cleanup stack for when this throws.
 *
 * @throws HRESULT
 */
void Appliance::i_importMachineGeneric(const ovf::VirtualSystem &vsysThis,
                                       ComObjPtr<VirtualSystemDescription> &vsdescThis,
                                       ComPtr<IMachine> &pNewMachineRet,
                                       ImportStack &stack)
{
    LogFlowFuncEnter();
    HRESULT hrc;

    // Get the instance of IGuestOSType which matches our string guest OS type so we
    // can use recommended defaults for the new machine where OVF doesn't provide any
    ComPtr<IGuestOSType> osType;
    hrc = mVirtualBox->GetGuestOSType(Bstr(stack.strOsTypeVBox).raw(), osType.asOutParam());
    if (FAILED(hrc)) throw hrc;

    /* Create the machine */
    SafeArray<BSTR> groups; /* no groups, or maybe one group... */
    if (!stack.strPrimaryGroup.isEmpty() && stack.strPrimaryGroup != "/")
        Bstr(stack.strPrimaryGroup).detachTo(groups.appendedRaw());
    ComPtr<IMachine> pNewMachine;
    hrc = mVirtualBox->CreateMachine(Bstr(stack.strSettingsFilename).raw(),
                                     Bstr(stack.strNameVBox).raw(),
                                     PlatformArchitecture_x86, /// @todo BUGBUG Only x86 for now!
                                     ComSafeArrayAsInParam(groups),
                                     Bstr(stack.strOsTypeVBox).raw(),
                                     NULL, /* aCreateFlags */
                                     NULL, /* aCipher */
                                     NULL, /* aPasswordId */
                                     NULL, /* aPassword */
                                     pNewMachine.asOutParam());
    if (FAILED(hrc)) throw hrc;
    pNewMachineRet = pNewMachine;

    ComPtr<IPlatform> pPlatform;
    hrc = pNewMachine->COMGETTER(Platform)(pPlatform.asOutParam());
    if (FAILED(hrc)) throw hrc;

    ComPtr<IPlatformX86> pPlatformX86; /// @todo BUGBUG Only x86 for now! */
    hrc = pPlatform->COMGETTER(X86)(pPlatformX86.asOutParam());
    if (FAILED(hrc)) throw hrc;

    // set the description
    if (!stack.strDescription.isEmpty())
    {
        hrc = pNewMachine->COMSETTER(Description)(Bstr(stack.strDescription).raw());
        if (FAILED(hrc)) throw hrc;
    }

    // CPU count
    hrc = pNewMachine->COMSETTER(CPUCount)(stack.cCPUs);
    if (FAILED(hrc)) throw hrc;

    if (stack.fForceHWVirt)
    {
        hrc = pPlatformX86->SetHWVirtExProperty(HWVirtExPropertyType_Enabled, TRUE);
        if (FAILED(hrc)) throw hrc;
    }

    // RAM
    hrc = pNewMachine->COMSETTER(MemorySize)(stack.ulMemorySizeMB);
    if (FAILED(hrc)) throw hrc;

    /* VRAM */
    /* Get the recommended VRAM for this guest OS type */
    ULONG vramVBox;
    hrc = osType->COMGETTER(RecommendedVRAM)(&vramVBox);
    if (FAILED(hrc)) throw hrc;

    /* Set the VRAM */
    ComPtr<IGraphicsAdapter> pGraphicsAdapter;
    hrc = pNewMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());
    if (FAILED(hrc)) throw hrc;
    hrc = pGraphicsAdapter->COMSETTER(VRAMSize)(vramVBox);
    if (FAILED(hrc)) throw hrc;

    // I/O APIC: Generic OVF has no setting for this. Enable it if we
    // import a Windows VM because if if Windows was installed without IOAPIC,
    // it will not mind finding an one later on, but if Windows was installed
    // _with_ an IOAPIC, it will bluescreen if it's not found
    if (!stack.fForceIOAPIC)
    {
        Bstr bstrFamilyId;
        hrc = osType->COMGETTER(FamilyId)(bstrFamilyId.asOutParam());
        if (FAILED(hrc)) throw hrc;
        if (bstrFamilyId == "Windows")
            stack.fForceIOAPIC = true;
    }

    ComPtr<IFirmwareSettings> pFirmwareSettings;
    hrc = pNewMachine->COMGETTER(FirmwareSettings)(pFirmwareSettings.asOutParam());
    if (FAILED(hrc)) throw hrc;

    if (stack.fForceIOAPIC)
    {
        hrc = pFirmwareSettings->COMSETTER(IOAPICEnabled)(TRUE);
        if (FAILED(hrc)) throw hrc;
    }

    if (stack.strFirmwareType.isNotEmpty())
    {
        FirmwareType_T firmwareType = FirmwareType_BIOS;
        if (stack.strFirmwareType.contains("EFI"))
        {
            if (stack.strFirmwareType.contains("32"))
                firmwareType = FirmwareType_EFI32;
            else if (stack.strFirmwareType.contains("64"))
                firmwareType = FirmwareType_EFI64;
            else
                firmwareType = FirmwareType_EFI;
        }
        hrc = pFirmwareSettings->COMSETTER(FirmwareType)(firmwareType);
        if (FAILED(hrc)) throw hrc;
    }

    if (stack.strNvramPath.isNotEmpty())
    {
        /* Construct source file path */
        Utf8Str strSrcFilePath;
        if (stack.hVfsFssOva != NIL_RTVFSFSSTREAM)
            strSrcFilePath = stack.strNvramPath;
        else
        {
            strSrcFilePath = stack.strSourceDir;
            strSrcFilePath.append(RTPATH_SLASH_STR);
            strSrcFilePath.append(stack.strNvramPath);
        }

        /* The basename of the destination filename needs to be the VM's name in
         * order to match the VM's INvramStore::nonVolatileStorageFile attribute so
         * that EFI can find it when booting the VM. */
        Utf8Str strAbsDstPath;
        Utf8Str strDstFilename = stack.strNameVBox;
        strDstFilename += ".nvram";
        int vrc = RTPathAbsExCxx(strAbsDstPath, stack.strMachineFolder, strDstFilename);
        AssertRCStmt(vrc, throw Global::vboxStatusCodeToCOM(vrc));

        i_importCopyFile(stack, strSrcFilePath, strAbsDstPath, stack.strNvramPath.c_str());
    }

    if (!stack.strAudioAdapter.isEmpty())
        if (stack.strAudioAdapter.compare("null", Utf8Str::CaseInsensitive) != 0)
        {
            ComPtr<IAudioSettings> audioSettings;
            hrc = pNewMachine->COMGETTER(AudioSettings)(audioSettings.asOutParam());
            if (FAILED(hrc)) throw hrc;
            uint32_t audio = RTStrToUInt32(stack.strAudioAdapter.c_str());       // should be 0 for AC97
            ComPtr<IAudioAdapter> audioAdapter;
            hrc = audioSettings->COMGETTER(Adapter)(audioAdapter.asOutParam());
            if (FAILED(hrc)) throw hrc;
            hrc = audioAdapter->COMSETTER(Enabled)(true);
            if (FAILED(hrc)) throw hrc;
            hrc = audioAdapter->COMSETTER(AudioController)(static_cast<AudioControllerType_T>(audio));
            if (FAILED(hrc)) throw hrc;
        }

#ifdef VBOX_WITH_USB
    /* USB Controller */
    if (stack.fUSBEnabled)
    {
        ComPtr<IUSBController> usbController;
        hrc = pNewMachine->AddUSBController(Bstr("OHCI").raw(), USBControllerType_OHCI, usbController.asOutParam());
        if (FAILED(hrc)) throw hrc;
    }
#endif /* VBOX_WITH_USB */

    /* Change the network adapters */
    uint32_t const maxNetworkAdapters = PlatformProperties::s_getMaxNetworkAdapters(ChipsetType_PIIX3); /** @todo BUGBUG Only x86 for now! */

    std::list<VirtualSystemDescriptionEntry*> vsdeNW = vsdescThis->i_findByType(VirtualSystemDescriptionType_NetworkAdapter);
    if (vsdeNW.empty())
    {
        /* No network adapters, so we have to disable our default one */
        ComPtr<INetworkAdapter> nwVBox;
        hrc = pNewMachine->GetNetworkAdapter(0, nwVBox.asOutParam());
        if (FAILED(hrc)) throw hrc;
        hrc = nwVBox->COMSETTER(Enabled)(false);
        if (FAILED(hrc)) throw hrc;
    }
    else if (vsdeNW.size() > maxNetworkAdapters)
        throw setError(VBOX_E_FILE_ERROR,
                       tr("Too many network adapters: OVF requests %d network adapters, "
                          "but VirtualBox only supports %d", "", vsdeNW.size()),
                       vsdeNW.size(), maxNetworkAdapters);
    else
    {
        list<VirtualSystemDescriptionEntry*>::const_iterator nwIt;
        size_t a = 0;
        for (nwIt = vsdeNW.begin();
             nwIt != vsdeNW.end();
             ++nwIt, ++a)
        {
            const VirtualSystemDescriptionEntry* pvsys = *nwIt;

            const Utf8Str &nwTypeVBox = pvsys->strVBoxCurrent;
            uint32_t tt1 = RTStrToUInt32(nwTypeVBox.c_str());
            ComPtr<INetworkAdapter> pNetworkAdapter;
            hrc = pNewMachine->GetNetworkAdapter((ULONG)a, pNetworkAdapter.asOutParam());
            if (FAILED(hrc)) throw hrc;
            /* Enable the network card & set the adapter type */
            hrc = pNetworkAdapter->COMSETTER(Enabled)(true);
            if (FAILED(hrc)) throw hrc;
            hrc = pNetworkAdapter->COMSETTER(AdapterType)(static_cast<NetworkAdapterType_T>(tt1));
            if (FAILED(hrc)) throw hrc;

            // default is NAT; change to "bridged" if extra conf says so
            if (pvsys->strExtraConfigCurrent.endsWith("type=Bridged", Utf8Str::CaseInsensitive))
            {
                /* Attach to the right interface */
                hrc = pNetworkAdapter->COMSETTER(AttachmentType)(NetworkAttachmentType_Bridged);
                if (FAILED(hrc)) throw hrc;
                ComPtr<IHost> host;
                hrc = mVirtualBox->COMGETTER(Host)(host.asOutParam());
                if (FAILED(hrc)) throw hrc;
                com::SafeIfaceArray<IHostNetworkInterface> nwInterfaces;
                hrc = host->COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(nwInterfaces));
                if (FAILED(hrc)) throw hrc;
                // We search for the first host network interface which
                // is usable for bridged networking
                for (size_t j = 0;
                     j < nwInterfaces.size();
                     ++j)
                {
                    HostNetworkInterfaceType_T itype;
                    hrc = nwInterfaces[j]->COMGETTER(InterfaceType)(&itype);
                    if (FAILED(hrc)) throw hrc;
                    if (itype == HostNetworkInterfaceType_Bridged)
                    {
                        Bstr name;
                        hrc = nwInterfaces[j]->COMGETTER(Name)(name.asOutParam());
                        if (FAILED(hrc)) throw hrc;
                        /* Set the interface name to attach to */
                        hrc = pNetworkAdapter->COMSETTER(BridgedInterface)(name.raw());
                        if (FAILED(hrc)) throw hrc;
                        break;
                    }
                }
            }
            /* Next test for host only interfaces */
            else if (pvsys->strExtraConfigCurrent.endsWith("type=HostOnly", Utf8Str::CaseInsensitive))
            {
                /* Attach to the right interface */
                hrc = pNetworkAdapter->COMSETTER(AttachmentType)(NetworkAttachmentType_HostOnly);
                if (FAILED(hrc)) throw hrc;
                ComPtr<IHost> host;
                hrc = mVirtualBox->COMGETTER(Host)(host.asOutParam());
                if (FAILED(hrc)) throw hrc;
                com::SafeIfaceArray<IHostNetworkInterface> nwInterfaces;
                hrc = host->COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(nwInterfaces));
                if (FAILED(hrc)) throw hrc;
                // We search for the first host network interface which
                // is usable for host only networking
                for (size_t j = 0;
                     j < nwInterfaces.size();
                     ++j)
                {
                    HostNetworkInterfaceType_T itype;
                    hrc = nwInterfaces[j]->COMGETTER(InterfaceType)(&itype);
                    if (FAILED(hrc)) throw hrc;
                    if (itype == HostNetworkInterfaceType_HostOnly)
                    {
                        Bstr name;
                        hrc = nwInterfaces[j]->COMGETTER(Name)(name.asOutParam());
                        if (FAILED(hrc)) throw hrc;
                        /* Set the interface name to attach to */
                        hrc = pNetworkAdapter->COMSETTER(HostOnlyInterface)(name.raw());
                        if (FAILED(hrc)) throw hrc;
                        break;
                    }
                }
            }
            /* Next test for internal interfaces */
            else if (pvsys->strExtraConfigCurrent.endsWith("type=Internal", Utf8Str::CaseInsensitive))
            {
                /* Attach to the right interface */
                hrc = pNetworkAdapter->COMSETTER(AttachmentType)(NetworkAttachmentType_Internal);
                if (FAILED(hrc)) throw hrc;
            }
            /* Next test for Generic interfaces */
            else if (pvsys->strExtraConfigCurrent.endsWith("type=Generic", Utf8Str::CaseInsensitive))
            {
                /* Attach to the right interface */
                hrc = pNetworkAdapter->COMSETTER(AttachmentType)(NetworkAttachmentType_Generic);
                if (FAILED(hrc)) throw hrc;
            }

            /* Next test for NAT network interfaces */
            else if (pvsys->strExtraConfigCurrent.endsWith("type=NATNetwork", Utf8Str::CaseInsensitive))
            {
                /* Attach to the right interface */
                hrc = pNetworkAdapter->COMSETTER(AttachmentType)(NetworkAttachmentType_NATNetwork);
                if (FAILED(hrc)) throw hrc;
                com::SafeIfaceArray<INATNetwork> nwNATNetworks;
                hrc = mVirtualBox->COMGETTER(NATNetworks)(ComSafeArrayAsOutParam(nwNATNetworks));
                if (FAILED(hrc)) throw hrc;
                // Pick the first NAT network (if there is any)
                if (nwNATNetworks.size())
                {
                    Bstr name;
                    hrc = nwNATNetworks[0]->COMGETTER(NetworkName)(name.asOutParam());
                    if (FAILED(hrc)) throw hrc;
                    /* Set the NAT network name to attach to */
                    hrc = pNetworkAdapter->COMSETTER(NATNetwork)(name.raw());
                    if (FAILED(hrc)) throw hrc;
                    break;
                }
            }
        }
    }

    // Storage controller IDE
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCIDE =
        vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskControllerIDE);
    /*
     * In OVF (at least VMware's version of it), an IDE controller has two ports,
     * so VirtualBox's single IDE controller with two channels and two ports each counts as
     * two OVF IDE controllers -- so we accept one or two such IDE controllers
     */
    size_t cIDEControllers = vsdeHDCIDE.size();
    if (cIDEControllers > 2)
        throw setError(VBOX_E_FILE_ERROR,
                       tr("Too many IDE controllers in OVF; import facility only supports two"));
    if (!vsdeHDCIDE.empty())
    {
        // one or two IDE controllers present in OVF: add one VirtualBox controller
        ComPtr<IStorageController> pController;
        hrc = pNewMachine->AddStorageController(Bstr("IDE").raw(), StorageBus_IDE, pController.asOutParam());
        if (FAILED(hrc)) throw hrc;

        const char *pcszIDEType = vsdeHDCIDE.front()->strVBoxCurrent.c_str();
        if (!strcmp(pcszIDEType, "PIIX3"))
            hrc = pController->COMSETTER(ControllerType)(StorageControllerType_PIIX3);
        else if (!strcmp(pcszIDEType, "PIIX4"))
            hrc = pController->COMSETTER(ControllerType)(StorageControllerType_PIIX4);
        else if (!strcmp(pcszIDEType, "ICH6"))
            hrc = pController->COMSETTER(ControllerType)(StorageControllerType_ICH6);
        else
            throw setError(VBOX_E_FILE_ERROR,
                           tr("Invalid IDE controller type \"%s\""),
                           pcszIDEType);
        if (FAILED(hrc)) throw hrc;
    }

    /* Storage controller SATA */
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCSATA =
        vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskControllerSATA);
    if (vsdeHDCSATA.size() > 1)
        throw setError(VBOX_E_FILE_ERROR,
                       tr("Too many SATA controllers in OVF; import facility only supports one"));
    if (!vsdeHDCSATA.empty())
    {
        ComPtr<IStorageController> pController;
        const Utf8Str &hdcVBox = vsdeHDCSATA.front()->strVBoxCurrent;
        if (hdcVBox == "AHCI")
        {
            hrc = pNewMachine->AddStorageController(Bstr("SATA").raw(), StorageBus_SATA, pController.asOutParam());
            if (FAILED(hrc)) throw hrc;
        }
        else
            throw setError(VBOX_E_FILE_ERROR, tr("Invalid SATA controller type \"%s\""), hdcVBox.c_str());
    }

    /* Storage controller SCSI */
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCSCSI =
        vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskControllerSCSI);
    if (vsdeHDCSCSI.size() > 1)
        throw setError(VBOX_E_FILE_ERROR,
                       tr("Too many SCSI controllers in OVF; import facility only supports one"));
    if (!vsdeHDCSCSI.empty())
    {
        ComPtr<IStorageController> pController;
        Utf8Str strName("SCSI");
        StorageBus_T busType = StorageBus_SCSI;
        StorageControllerType_T controllerType;
        const Utf8Str &hdcVBox = vsdeHDCSCSI.front()->strVBoxCurrent;
        if (hdcVBox == "LsiLogic")
            controllerType = StorageControllerType_LsiLogic;
        else if (hdcVBox == "LsiLogicSas")
        {
            // OVF treats LsiLogicSas as a SCSI controller but VBox considers it a class of its own
            strName = "SAS";
            busType = StorageBus_SAS;
            controllerType = StorageControllerType_LsiLogicSas;
        }
        else if (hdcVBox == "BusLogic")
            controllerType = StorageControllerType_BusLogic;
        else
            throw setError(VBOX_E_FILE_ERROR, tr("Invalid SCSI controller type \"%s\""), hdcVBox.c_str());

        hrc = pNewMachine->AddStorageController(Bstr(strName).raw(), busType, pController.asOutParam());
        if (FAILED(hrc)) throw hrc;
        hrc = pController->COMSETTER(ControllerType)(controllerType);
        if (FAILED(hrc)) throw hrc;
    }

    /* Storage controller SAS */
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCSAS =
        vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskControllerSAS);
    if (vsdeHDCSAS.size() > 1)
        throw setError(VBOX_E_FILE_ERROR,
                       tr("Too many SAS controllers in OVF; import facility only supports one"));
    if (!vsdeHDCSAS.empty())
    {
        ComPtr<IStorageController> pController;
        hrc = pNewMachine->AddStorageController(Bstr(L"SAS").raw(), StorageBus_SAS, pController.asOutParam());
        if (FAILED(hrc)) throw hrc;
        hrc = pController->COMSETTER(ControllerType)(StorageControllerType_LsiLogicSas);
        if (FAILED(hrc)) throw hrc;
    }


    /* Storage controller VirtioSCSI */
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCVirtioSCSI =
        vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI);
    if (vsdeHDCVirtioSCSI.size() > 1)
        throw setError(VBOX_E_FILE_ERROR,
                       tr("Too many VirtioSCSI controllers in OVF; import facility only supports one"));
    if (!vsdeHDCVirtioSCSI.empty())
    {
        ComPtr<IStorageController> pController;
        Utf8Str strName("VirtioSCSI");
        const Utf8Str &hdcVBox = vsdeHDCVirtioSCSI.front()->strVBoxCurrent;
        if (hdcVBox == "VirtioSCSI")
        {
            hrc = pNewMachine->AddStorageController(Bstr(strName).raw(), StorageBus_VirtioSCSI, pController.asOutParam());
            if (FAILED(hrc)) throw hrc;

            hrc = pController->COMSETTER(ControllerType)(StorageControllerType_VirtioSCSI);
            if (FAILED(hrc)) throw hrc;
        }
        else
            throw setError(VBOX_E_FILE_ERROR, tr("Invalid VirtioSCSI controller type \"%s\""), hdcVBox.c_str());
    }

    /* Storage controller NVMe */
    std::list<VirtualSystemDescriptionEntry*> vsdeHDCNVMe =
        vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskControllerNVMe);
    if (vsdeHDCNVMe.size() > 1)
        throw setError(VBOX_E_FILE_ERROR,
                       tr("Too many NVMe controllers in OVF; import facility only supports one"));
    if (!vsdeHDCNVMe.empty())
    {
        ComPtr<IStorageController> pController;
        Utf8Str strName("NVMe");
        const Utf8Str &hdcVBox = vsdeHDCNVMe.front()->strVBoxCurrent;
        if (hdcVBox == "NVMe")
        {
            hrc = pNewMachine->AddStorageController(Bstr(strName).raw(), StorageBus_PCIe, pController.asOutParam());
            if (FAILED(hrc)) throw hrc;

            hrc = pController->COMSETTER(ControllerType)(StorageControllerType_NVMe);
            if (FAILED(hrc)) throw hrc;
        }
        else
            throw setError(VBOX_E_FILE_ERROR, tr("Invalid NVMe controller type \"%s\""), hdcVBox.c_str());
    }

    /* Now its time to register the machine before we add any storage devices */
    hrc = mVirtualBox->RegisterMachine(pNewMachine);
    if (FAILED(hrc)) throw hrc;

    // store new machine for roll-back in case of errors
    Bstr bstrNewMachineId;
    hrc = pNewMachine->COMGETTER(Id)(bstrNewMachineId.asOutParam());
    if (FAILED(hrc)) throw hrc;
    Guid uuidNewMachine(bstrNewMachineId);
    m->llGuidsMachinesCreated.push_back(uuidNewMachine);

    // Add floppies and CD-ROMs to the appropriate controllers.
    std::list<VirtualSystemDescriptionEntry*> vsdeFloppy = vsdescThis->i_findByType(VirtualSystemDescriptionType_Floppy);
    if (vsdeFloppy.size() > 1)
        throw setError(VBOX_E_FILE_ERROR,
                       tr("Too many floppy controllers in OVF; import facility only supports one"));
    std::list<VirtualSystemDescriptionEntry*> vsdeCDROM = vsdescThis->i_findByType(VirtualSystemDescriptionType_CDROM);
    if (    !vsdeFloppy.empty()
         || !vsdeCDROM.empty()
       )
    {
        // If there's an error here we need to close the session, so
        // we need another try/catch block.

        try
        {
            // to attach things we need to open a session for the new machine
            hrc = pNewMachine->LockMachine(stack.pSession, LockType_Write);
            if (FAILED(hrc)) throw hrc;
            stack.fSessionOpen = true;

            ComPtr<IMachine> sMachine;
            hrc = stack.pSession->COMGETTER(Machine)(sMachine.asOutParam());
            if (FAILED(hrc)) throw hrc;

            // floppy first
            if (vsdeFloppy.size() == 1)
            {
                ComPtr<IStorageController> pController;
                hrc = sMachine->AddStorageController(Bstr("Floppy").raw(), StorageBus_Floppy, pController.asOutParam());
                if (FAILED(hrc)) throw hrc;

                Bstr bstrName;
                hrc = pController->COMGETTER(Name)(bstrName.asOutParam());
                if (FAILED(hrc)) throw hrc;

                // this is for rollback later
                MyHardDiskAttachment mhda;
                mhda.pMachine = pNewMachine;
                mhda.controllerName = bstrName;
                mhda.lControllerPort = 0;
                mhda.lDevice = 0;

                Log(("Attaching floppy\n"));

                hrc = sMachine->AttachDevice(Bstr(mhda.controllerName).raw(),
                                             mhda.lControllerPort,
                                             mhda.lDevice,
                                             DeviceType_Floppy,
                                             NULL);
                if (FAILED(hrc)) throw hrc;

                stack.llHardDiskAttachments.push_back(mhda);
            }

            hrc = sMachine->SaveSettings();
            if (FAILED(hrc)) throw hrc;

            // only now that we're done with all storage devices, close the session
            hrc = stack.pSession->UnlockMachine();
            if (FAILED(hrc)) throw hrc;
            stack.fSessionOpen = false;
        }
        catch (HRESULT hrcXcpt)
        {
            com::ErrorInfo info;

            if (stack.fSessionOpen)
                stack.pSession->UnlockMachine();

            if (info.isFullAvailable())
                throw setError(hrcXcpt, Utf8Str(info.getText()).c_str());
            else
                throw setError(hrcXcpt, tr("Unknown error during OVF import"));
        }
    }

    // create the storage devices & connect them to the appropriate controllers
    std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskImage);
    if (!avsdeHDs.empty())
    {
        // If there's an error here we need to close the session, so
        // we need another try/catch block.
        try
        {
#ifdef LOG_ENABLED
            if (LogIsEnabled())
            {
                size_t i = 0;
                for (list<VirtualSystemDescriptionEntry*>::const_iterator itHD = avsdeHDs.begin();
                     itHD != avsdeHDs.end(); ++itHD, i++)
                     Log(("avsdeHDs[%zu]: strRef=%s strOvf=%s\n", i, (*itHD)->strRef.c_str(), (*itHD)->strOvf.c_str()));
                i = 0;
                for (ovf::DiskImagesMap::const_iterator itDisk = stack.mapDisks.begin(); itDisk != stack.mapDisks.end(); ++itDisk)
                    Log(("mapDisks[%zu]: strDiskId=%s strHref=%s\n",
                         i, itDisk->second.strDiskId.c_str(), itDisk->second.strHref.c_str()));

            }
#endif

            // to attach things we need to open a session for the new machine
            hrc = pNewMachine->LockMachine(stack.pSession, LockType_Write);
            if (FAILED(hrc)) throw hrc;
            stack.fSessionOpen = true;

            /* get VM name from virtual system description. Only one record is possible (size of list is equal 1). */
            std::list<VirtualSystemDescriptionEntry*> vmName = vsdescThis->i_findByType(VirtualSystemDescriptionType_Name);
            std::list<VirtualSystemDescriptionEntry*>::iterator vmNameIt = vmName.begin();
            VirtualSystemDescriptionEntry* vmNameEntry = *vmNameIt;


            ovf::DiskImagesMap::const_iterator oit = stack.mapDisks.begin();
            std::set<RTCString>  disksResolvedNames;

            uint32_t cImportedDisks = 0;

            while (oit != stack.mapDisks.end() && cImportedDisks != avsdeHDs.size())
            {
/** @todo r=bird: Most of the code here is duplicated in the other machine
 *        import method, factor out. */
                ovf::DiskImage diCurrent = oit->second;

                Log(("diCurrent.strDiskId=%s diCurrent.strHref=%s\n", diCurrent.strDiskId.c_str(), diCurrent.strHref.c_str()));
                /* Iterate over all given images of the virtual system
                 * description. We need to find the target image path,
                 * which could be changed by the user. */
                VirtualSystemDescriptionEntry *vsdeTargetHD = NULL;
                for (list<VirtualSystemDescriptionEntry*>::const_iterator itHD = avsdeHDs.begin();
                     itHD != avsdeHDs.end();
                     ++itHD)
                {
                    VirtualSystemDescriptionEntry *vsdeHD = *itHD;
                    if (vsdeHD->strRef == diCurrent.strDiskId)
                    {
                        vsdeTargetHD = vsdeHD;
                        break;
                    }
                }
                if (!vsdeTargetHD)
                {
                    /* possible case if an image belongs to other virtual system (OVF package with multiple VMs inside) */
                    Log1Warning(("OVA/OVF import: Disk image %s was missed during import of VM %s\n",
                                 oit->first.c_str(), vmNameEntry->strOvf.c_str()));
                    NOREF(vmNameEntry);
                    ++oit;
                    continue;
                }

                //diCurrent.strDiskId contains the image identifier (e.g. "vmdisk1"), which should exist
                //in the virtual system's images map under that ID and also in the global images map
                ovf::VirtualDisksMap::const_iterator itVDisk = vsysThis.mapVirtualDisks.find(diCurrent.strDiskId);
                if (itVDisk == vsysThis.mapVirtualDisks.end())
                    throw setError(E_FAIL,
                                   tr("Internal inconsistency looking up disk image '%s'"),
                                   diCurrent.strHref.c_str());

                /*
                 * preliminary check availability of the image
                 * This step is useful if image is placed in the OVA (TAR) package
                 */
                if (stack.hVfsFssOva != NIL_RTVFSFSSTREAM)
                {
                    /* It means that we possibly have imported the storage earlier on the previous loop steps*/
                    std::set<RTCString>::const_iterator h = disksResolvedNames.find(diCurrent.strHref);
                    if (h != disksResolvedNames.end())
                    {
                        /* Yes, image name was found, we can skip it*/
                        ++oit;
                        continue;
                    }
l_skipped:
                    hrc = i_preCheckImageAvailability(stack);
                    if (SUCCEEDED(hrc))
                    {
                        /* current opened file isn't the same as passed one */
                        if (RTStrICmp(diCurrent.strHref.c_str(), stack.pszOvaLookAheadName) != 0)
                        {
                            /* availableImage contains the image file reference (e.g. "disk1.vmdk"), which should
                             * exist in the global images map.
                             * And find the image from the OVF's disk list */
                            ovf::DiskImagesMap::const_iterator itDiskImage;
                            for (itDiskImage = stack.mapDisks.begin();
                                 itDiskImage != stack.mapDisks.end();
                                 itDiskImage++)
                                if (itDiskImage->second.strHref.compare(stack.pszOvaLookAheadName,
                                                                        Utf8Str::CaseInsensitive) == 0)
                                    break;
                            if (itDiskImage == stack.mapDisks.end())
                            {
                                LogFunc(("Skipping '%s'\n", stack.pszOvaLookAheadName));
                                RTVfsIoStrmRelease(stack.claimOvaLookAHead());
                                goto l_skipped;
                            }

                            /* replace with a new found image */
                            diCurrent = *(&itDiskImage->second);

                            /*
                             * Again iterate over all given images of the virtual system
                             * description using the found image
                             */
                            for (list<VirtualSystemDescriptionEntry*>::const_iterator itHD = avsdeHDs.begin();
                                 itHD != avsdeHDs.end();
                                 ++itHD)
                            {
                                VirtualSystemDescriptionEntry *vsdeHD = *itHD;
                                if (vsdeHD->strRef == diCurrent.strDiskId)
                                {
                                    vsdeTargetHD = vsdeHD;
                                    break;
                                }
                            }

                            /*
                             * in this case it's an error because something is wrong with the OVF description file.
                             * May be VBox imports OVA package with wrong file sequence inside the archive.
                             */
                            if (!vsdeTargetHD)
                                throw setError(E_FAIL,
                                               tr("Internal inconsistency looking up disk image '%s'"),
                                               diCurrent.strHref.c_str());

                            itVDisk = vsysThis.mapVirtualDisks.find(diCurrent.strDiskId);
                            if (itVDisk == vsysThis.mapVirtualDisks.end())
                                throw setError(E_FAIL,
                                               tr("Internal inconsistency looking up disk image '%s'"),
                                               diCurrent.strHref.c_str());
                        }
                        else
                        {
                            ++oit;
                        }
                    }
                    else
                    {
                        ++oit;
                        continue;
                    }
                }
                else
                {
                    /* just continue with normal files */
                    ++oit;
                }

                /* very important to store image name for the next checks */
                disksResolvedNames.insert(diCurrent.strHref);
////// end of duplicated code.
                const ovf::VirtualDisk &ovfVdisk = itVDisk->second;

                ComObjPtr<Medium> pTargetMedium;
                if (stack.locInfo.storageType == VFSType_Cloud)
                {
                    /* We have already all disks prepared (converted and registered in the VBox)
                     * and in the correct place (VM machine folder).
                     * so what is needed is to get the disk uuid from VirtualDisk::strDiskId
                     * and find the Medium object with this uuid.
                     * next just attach the Medium object to new VM.
                     * VirtualDisk::strDiskId is filled in the */

                    Guid id(ovfVdisk.strDiskId);
                    hrc = mVirtualBox->i_findHardDiskById(id, false, &pTargetMedium);
                    if (FAILED(hrc))
                        throw hrc;
                }
                else
                {
                    i_importOneDiskImage(diCurrent,
                                         vsdeTargetHD->strVBoxCurrent,
                                         pTargetMedium,
                                         stack);
                }

                // now use the new uuid to attach the medium to our new machine
                ComPtr<IMachine> sMachine;
                hrc = stack.pSession->COMGETTER(Machine)(sMachine.asOutParam());
                if (FAILED(hrc))
                    throw hrc;

                // this is for rollback later
                MyHardDiskAttachment mhda;
                mhda.pMachine = pNewMachine;

                // find the hard disk controller to which we should attach
                ovf::HardDiskController hdc;

                /*
                 * Before importing the virtual hard disk found above (diCurrent/vsdeTargetHD) first
                 * check if the user requested to change either the controller it is to be attached
                 * to and/or the controller port (aka 'channel') on the controller.
                 */
                if (   !vsdeTargetHD->strExtraConfigCurrent.isEmpty()
                    && vsdeTargetHD->strExtraConfigSuggested != vsdeTargetHD->strExtraConfigCurrent)
                {
                    int vrc;
                    uint32_t uTargetControllerIndex;
                    vrc = getStorageControllerDetailsFromStr(vsdeTargetHD->strExtraConfigCurrent, "controller=",
                        &uTargetControllerIndex);
                    if (RT_FAILURE(vrc))
                        throw setError(E_FAIL,
                                       tr("Target controller value invalid or missing: '%s'"),
                                       vsdeTargetHD->strExtraConfigCurrent.c_str());

                    uint32_t uNewControllerPortValue;
                    vrc = getStorageControllerDetailsFromStr(vsdeTargetHD->strExtraConfigCurrent, "channel=",
                        &uNewControllerPortValue);
                    if (RT_FAILURE(vrc))
                        throw setError(E_FAIL,
                                       tr("Target controller port ('channel=') invalid or missing: '%s'"),
                                       vsdeTargetHD->strExtraConfigCurrent.c_str());

                    const VirtualSystemDescriptionEntry *vsdeTargetController;
                    vsdeTargetController = vsdescThis->i_findByIndex(uTargetControllerIndex);
                    if (!vsdeTargetController)
                        throw setError(E_FAIL,
                                       tr("Failed to find storage controller '%u' in the System Description list"),
                                       uTargetControllerIndex);

                    hdc = (*vsysThis.mapControllers.find(vsdeTargetController->strRef.c_str())).second;

                    StorageControllerType_T hdStorageControllerType = StorageControllerType_Null;
                    switch (hdc.system)
                    {
                        case ovf::HardDiskController::IDE:
                            hdStorageControllerType = StorageControllerType_PIIX3;
                            break;
                        case ovf::HardDiskController::SATA:
                            hdStorageControllerType = StorageControllerType_IntelAhci;
                            break;
                        case ovf::HardDiskController::SCSI:
                        {
                            if (hdc.strControllerType.compare("lsilogicsas")==0)
                                hdStorageControllerType = StorageControllerType_LsiLogicSas;
                            else
                                hdStorageControllerType = StorageControllerType_LsiLogic;
                            break;
                        }
                        case ovf::HardDiskController::VIRTIOSCSI:
                            hdStorageControllerType = StorageControllerType_VirtioSCSI;
                            break;
                        case ovf::HardDiskController::NVMe:
                            hdStorageControllerType = StorageControllerType_NVMe;
                            break;
                        default:
                            throw setError(E_FAIL,
                                           tr("Invalid hard disk contoller type: '%d'"),
                                           hdc.system);
                            break;
                    }

                    ULONG ulMaxPorts;
                    hrc = i_verifyStorageControllerPortValid(hdStorageControllerType, uNewControllerPortValue, &ulMaxPorts);
                    if (FAILED(hrc))
                    {
                        if (hrc == E_INVALIDARG)
                        {
                            const char *pcszSCType = Global::stringifyStorageControllerType(hdStorageControllerType);
                            throw setError(E_INVALIDARG,
                                           tr("Illegal channel: '%u'.  For %s controllers the valid values are "
                                           "0 to %lu (inclusive).\n"), uNewControllerPortValue, pcszSCType, ulMaxPorts-1);
                        }
                        else
                            throw hrc;
                    }

                    unconst(ovfVdisk.ulAddressOnParent) = uNewControllerPortValue;
                }
                else
                    hdc = (*vsysThis.mapControllers.find(ovfVdisk.strIdController)).second;


                i_convertDiskAttachmentValues(hdc,
                                              ovfVdisk.ulAddressOnParent,
                                              mhda.controllerName,
                                              mhda.lControllerPort,
                                              mhda.lDevice);

                Log(("Attaching disk %s to port %d on device %d\n",
                     vsdeTargetHD->strVBoxCurrent.c_str(), mhda.lControllerPort, mhda.lDevice));

                DeviceType_T devType = DeviceType_Null;
                hrc = pTargetMedium->COMGETTER(DeviceType)(&devType);
                if (FAILED(hrc))
                    throw hrc;

                hrc = sMachine->AttachDevice(Bstr(mhda.controllerName).raw(),// name
                                             mhda.lControllerPort,     // long controllerPort
                                             mhda.lDevice,             // long device
                                             devType,                  // DeviceType_T type
                                             pTargetMedium);
                if (FAILED(hrc))
                    throw hrc;

                stack.llHardDiskAttachments.push_back(mhda);

                hrc = sMachine->SaveSettings();
                if (FAILED(hrc))
                    throw hrc;

                ++cImportedDisks;

            } // end while(oit != stack.mapDisks.end())

            /*
             * quantity of the imported disks isn't equal to the size of the avsdeHDs list.
             */
            if(cImportedDisks < avsdeHDs.size())
            {
                Log1Warning(("Not all disk images were imported for VM %s. Check OVF description file.",
                             vmNameEntry->strOvf.c_str()));
            }

            // only now that we're done with all disks, close the session
            hrc = stack.pSession->UnlockMachine();
            if (FAILED(hrc))
                throw hrc;
            stack.fSessionOpen = false;
        }
        catch (HRESULT hrcXcpt)
        {
            com::ErrorInfo info;
            if (stack.fSessionOpen)
                stack.pSession->UnlockMachine();

            if (info.isFullAvailable())
                throw setError(hrcXcpt, Utf8Str(info.getText()).c_str());
            else
                throw setError(hrcXcpt, tr("Unknown error during OVF import"));
        }
    }
    LogFlowFuncLeave();
}

/**
 * Imports one OVF virtual system (described by a vbox:Machine tag represented by the given config
 * structure) into VirtualBox by creating an IMachine instance, which is returned.
 *
 * This throws HRESULT error codes for anything that goes wrong, in which case the caller must clean
 * up any leftovers from this function. For this, the given ImportStack instance has received information
 * about what needs cleaning up (to support rollback).
 *
 * The machine config stored in the settings::MachineConfigFile structure contains the UUIDs of
 * the disk attachments used by the machine when it was exported. We also add vbox:uuid attributes
 * to the OVF disks sections so we can look them up. While importing these UUIDs into a second host
 * will most probably work, reimporting them into the same host will cause conflicts, so we always
 * generate new ones on import. This involves the following:
 *
 *  1)  Scan the machine config for disk attachments.
 *
 *  2)  For each disk attachment found, look up the OVF disk image from the disk references section
 *      and import the disk into VirtualBox, which creates a new UUID for it. In the machine config,
 *      replace the old UUID with the new one.
 *
 *  3)  Change the machine config according to the OVF virtual system descriptions, in case the
 *      caller has modified them using setFinalValues().
 *
 *  4)  Create the VirtualBox machine with the modfified machine config.
 *
 * @param   vsdescThis
 * @param   pReturnNewMachine
 * @param   stack
 */
void Appliance::i_importVBoxMachine(ComObjPtr<VirtualSystemDescription> &vsdescThis,
                                    ComPtr<IMachine> &pReturnNewMachine,
                                    ImportStack &stack)
{
    LogFlowFuncEnter();
    Assert(vsdescThis->m->pConfig);

    HRESULT hrc = S_OK;

    settings::MachineConfigFile &config = *vsdescThis->m->pConfig;

    /*
     * step 1): modify machine config according to OVF config, in case the user
     * has modified them using setFinalValues()
     */

    /* OS Type */
    config.machineUserData.strOsType = stack.strOsTypeVBox;
    /* Groups */
    if (stack.strPrimaryGroup.isEmpty() || stack.strPrimaryGroup == "/")
    {
        config.machineUserData.llGroups.clear();
        config.machineUserData.llGroups.push_back("/");
    }
    else
    {
        /* Replace the primary group if there is one, otherwise add it. */
        if (config.machineUserData.llGroups.size())
            config.machineUserData.llGroups.pop_front();
        config.machineUserData.llGroups.push_front(stack.strPrimaryGroup);
    }
    /* Description */
    config.machineUserData.strDescription = stack.strDescription;
    /* CPU count & extented attributes */
    config.hardwareMachine.cCPUs = stack.cCPUs;
    if (stack.fForceIOAPIC)
        config.hardwareMachine.platformSettings.x86.fHWVirtEx = true;
    if (stack.fForceIOAPIC)
        config.hardwareMachine.firmwareSettings.fIOAPICEnabled = true;
    /* RAM size */
    config.hardwareMachine.ulMemorySizeMB = stack.ulMemorySizeMB;

/*
    <const name="HardDiskControllerIDE" value="14" />
    <const name="HardDiskControllerSATA" value="15" />
    <const name="HardDiskControllerSCSI" value="16" />
    <const name="HardDiskControllerSAS" value="17" />
    <const name="HardDiskControllerVirtioSCSI" value="60" />
*/

#ifdef VBOX_WITH_USB
    /* USB controller */
    if (stack.fUSBEnabled)
    {
        /** @todo r=klaus add support for arbitrary USB controller types, this can't handle
         *  multiple controllers due to its design anyway */
        /* Usually the OHCI controller is enabled already, need to check. But
         * do this only if there is no xHCI controller. */
        bool fOHCIEnabled = false;
        bool fXHCIEnabled = false;
        settings::USBControllerList &llUSBControllers = config.hardwareMachine.usbSettings.llUSBControllers;
        settings::USBControllerList::iterator it;
        for (it = llUSBControllers.begin(); it != llUSBControllers.end(); ++it)
        {
            if (it->enmType == USBControllerType_OHCI)
                fOHCIEnabled = true;
            if (it->enmType == USBControllerType_XHCI)
                fXHCIEnabled = true;
        }

        if (!fXHCIEnabled && !fOHCIEnabled)
        {
            settings::USBController ctrl;
            ctrl.strName = "OHCI";
            ctrl.enmType = USBControllerType_OHCI;

            llUSBControllers.push_back(ctrl);
        }
    }
    else
        config.hardwareMachine.usbSettings.llUSBControllers.clear();
#endif
    /* Audio adapter */
    if (stack.strAudioAdapter.isNotEmpty())
    {
        config.hardwareMachine.audioAdapter.fEnabled = true;
        config.hardwareMachine.audioAdapter.controllerType = (AudioControllerType_T)stack.strAudioAdapter.toUInt32();
    }
    else
        config.hardwareMachine.audioAdapter.fEnabled = false;
    /* Network adapter */
    settings::NetworkAdaptersList &llNetworkAdapters = config.hardwareMachine.llNetworkAdapters;
    /* First disable all network cards, they will be enabled below again. */
    settings::NetworkAdaptersList::iterator it1;
    bool fKeepAllMACs = m->optListImport.contains(ImportOptions_KeepAllMACs);
    bool fKeepNATMACs = m->optListImport.contains(ImportOptions_KeepNATMACs);
    for (it1 = llNetworkAdapters.begin(); it1 != llNetworkAdapters.end(); ++it1)
    {
        it1->fEnabled = false;
        if (!(   fKeepAllMACs
              || (fKeepNATMACs && it1->mode == NetworkAttachmentType_NAT)
              || (fKeepNATMACs && it1->mode == NetworkAttachmentType_NATNetwork)))
            /* Force generation of new MAC address below. */
            it1->strMACAddress.setNull();
    }
    /* Now iterate over all network entries. */
    std::list<VirtualSystemDescriptionEntry*> avsdeNWs = vsdescThis->i_findByType(VirtualSystemDescriptionType_NetworkAdapter);
    if (!avsdeNWs.empty())
    {
        /* Iterate through all network adapter entries and search for the
         * corresponding one in the machine config. If one is found, configure
         * it based on the user settings. */
        list<VirtualSystemDescriptionEntry*>::const_iterator itNW;
        for (itNW = avsdeNWs.begin();
             itNW != avsdeNWs.end();
             ++itNW)
        {
            VirtualSystemDescriptionEntry *vsdeNW = *itNW;
            if (   vsdeNW->strExtraConfigCurrent.startsWith("slot=", Utf8Str::CaseInsensitive)
                && vsdeNW->strExtraConfigCurrent.length() > 6)
            {
                uint32_t iSlot = vsdeNW->strExtraConfigCurrent.substr(5).toUInt32();
                /* Iterate through all network adapters in the machine config. */
                for (it1 = llNetworkAdapters.begin();
                     it1 != llNetworkAdapters.end();
                     ++it1)
                {
                    /* Compare the slots. */
                    if (it1->ulSlot == iSlot)
                    {
                        it1->fEnabled = true;
                        if (it1->strMACAddress.isEmpty())
                            Host::i_generateMACAddress(it1->strMACAddress);
                        it1->type = (NetworkAdapterType_T)vsdeNW->strVBoxCurrent.toUInt32();
                        break;
                    }
                }
            }
        }
    }

    /* Import the NRAM file if present */
    if (stack.strNvramPath.isNotEmpty())
    {
        /* Construct source file path */
        Utf8Str strSrcFilePath;
        if (stack.hVfsFssOva != NIL_RTVFSFSSTREAM)
            strSrcFilePath = stack.strNvramPath;
        else
        {
            strSrcFilePath = stack.strSourceDir;
            strSrcFilePath.append(RTPATH_SLASH_STR);
            strSrcFilePath.append(stack.strNvramPath);
        }

        /* The basename of the destination filename needs to be the VM's name in
         * order to match the VM's INvramStore::nonVolatileStorageFile attribute so
         * that EFI can find it when booting the VM. */
        Utf8Str strAbsDstPath;
        Utf8Str strDstFilename = stack.strNameVBox;
        strDstFilename += ".nvram";
        int vrc = RTPathAbsExCxx(strAbsDstPath, stack.strMachineFolder, strDstFilename);
        AssertRCStmt(vrc, throw Global::vboxStatusCodeToCOM(vrc));

        i_importCopyFile(stack, strSrcFilePath, strAbsDstPath, stack.strNvramPath.c_str());
    }

    /* Floppy controller */
    bool fFloppy = vsdescThis->i_findByType(VirtualSystemDescriptionType_Floppy).size() > 0;
    /* DVD controller */
    bool fDVD = vsdescThis->i_findByType(VirtualSystemDescriptionType_CDROM).size() > 0;
    /* Iterate over all storage controller check the attachments and remove
     * them when necessary. Also detect broken configs with more than one
     * attachment. Old VirtualBox versions (prior to 3.2.10) had all disk
     * attachments pointing to the last hard disk image, which causes import
     * failures. A long fixed bug, however the OVF files are long lived. */
    settings::StorageControllersList &llControllers = config.hardwareMachine.storage.llStorageControllers;
    uint32_t cDisks = 0;
    bool fInconsistent = false;
    bool fRepairDuplicate = false;
    settings::StorageControllersList::iterator it3;
    for (it3 = llControllers.begin();
         it3 != llControllers.end();
         ++it3)
    {
        Guid hdUuid;
        settings::AttachedDevicesList &llAttachments = it3->llAttachedDevices;
        settings::AttachedDevicesList::iterator it4 = llAttachments.begin();
        while (it4 != llAttachments.end())
        {
            if (  (   !fDVD
                   && it4->deviceType == DeviceType_DVD)
                ||
                  (   !fFloppy
                   && it4->deviceType == DeviceType_Floppy))
            {
                it4 = llAttachments.erase(it4);
                continue;
            }
            else if (it4->deviceType == DeviceType_HardDisk)
            {
                const Guid &thisUuid = it4->uuid;
                cDisks++;
                if (cDisks == 1)
                {
                    if (hdUuid.isZero())
                        hdUuid = thisUuid;
                    else
                        fInconsistent = true;
                }
                else
                {
                   if (thisUuid.isZero())
                        fInconsistent = true;
                    else if (thisUuid == hdUuid)
                        fRepairDuplicate = true;
                }
            }
            ++it4;
        }
    }
    /* paranoia... */
    if (fInconsistent || cDisks == 1)
        fRepairDuplicate = false;

    /*
     * step 2: scan the machine config for media attachments
     */
    /* get VM name from virtual system description. Only one record is possible (size of list is equal 1). */
    std::list<VirtualSystemDescriptionEntry*> vmName = vsdescThis->i_findByType(VirtualSystemDescriptionType_Name);
    std::list<VirtualSystemDescriptionEntry*>::iterator vmNameIt = vmName.begin();
    VirtualSystemDescriptionEntry* vmNameEntry = *vmNameIt;

    /* Get all hard disk descriptions. */
    std::list<VirtualSystemDescriptionEntry*> avsdeHDs = vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskImage);
    std::list<VirtualSystemDescriptionEntry*>::iterator avsdeHDsIt = avsdeHDs.begin();
    /* paranoia - if there is no 1:1 match do not try to repair. */
    if (cDisks != avsdeHDs.size())
        fRepairDuplicate = false;

    // there must be an image in the OVF disk structs with the same UUID

    ovf::DiskImagesMap::const_iterator oit = stack.mapDisks.begin();
    std::set<RTCString>  disksResolvedNames;

    uint32_t cImportedDisks = 0;

    while (oit != stack.mapDisks.end() && cImportedDisks != avsdeHDs.size())
    {
/** @todo r=bird: Most of the code here is duplicated in the other machine
 *        import method, factor out. */
        ovf::DiskImage diCurrent = oit->second;

        Log(("diCurrent.strDiskId=%s diCurrent.strHref=%s\n", diCurrent.strDiskId.c_str(), diCurrent.strHref.c_str()));

        /* Iterate over all given disk images of the virtual system
         * disks description. We need to find the target disk path,
         * which could be changed by the user. */
        VirtualSystemDescriptionEntry *vsdeTargetHD = NULL;
        for (list<VirtualSystemDescriptionEntry*>::const_iterator itHD = avsdeHDs.begin();
             itHD != avsdeHDs.end();
             ++itHD)
        {
            VirtualSystemDescriptionEntry *vsdeHD = *itHD;
            if (vsdeHD->strRef == oit->first)
            {
                vsdeTargetHD = vsdeHD;
                break;
            }
        }
        if (!vsdeTargetHD)
        {
            /* possible case if a disk image belongs to other virtual system (OVF package with multiple VMs inside) */
            Log1Warning(("OVA/OVF import: Disk image %s was missed during import of VM %s\n",
                         oit->first.c_str(), vmNameEntry->strOvf.c_str()));
            NOREF(vmNameEntry);
            ++oit;
            continue;
        }

        /*
         * preliminary check availability of the image
         * This step is useful if image is placed in the OVA (TAR) package
         */
        if (stack.hVfsFssOva != NIL_RTVFSFSSTREAM)
        {
            /* It means that we possibly have imported the storage earlier on a previous loop step. */
            std::set<RTCString>::const_iterator h = disksResolvedNames.find(diCurrent.strHref);
            if (h != disksResolvedNames.end())
            {
                /* Yes, disk name was found, we can skip it*/
                ++oit;
                continue;
            }
l_skipped:
            hrc = i_preCheckImageAvailability(stack);
            if (SUCCEEDED(hrc))
            {
                /* current opened file isn't the same as passed one */
                if (RTStrICmp(diCurrent.strHref.c_str(), stack.pszOvaLookAheadName) != 0)
                {
                    // availableImage contains the disk identifier (e.g. "vmdisk1"), which should exist
                    // in the virtual system's disks map under that ID and also in the global images map
                    // and find the disk from the OVF's disk list
                    ovf::DiskImagesMap::const_iterator itDiskImage;
                    for (itDiskImage = stack.mapDisks.begin();
                         itDiskImage != stack.mapDisks.end();
                         itDiskImage++)
                        if (itDiskImage->second.strHref.compare(stack.pszOvaLookAheadName,
                                                                Utf8Str::CaseInsensitive) == 0)
                            break;
                    if (itDiskImage == stack.mapDisks.end())
                    {
                        LogFunc(("Skipping '%s'\n", stack.pszOvaLookAheadName));
                        RTVfsIoStrmRelease(stack.claimOvaLookAHead());
                        goto l_skipped;
                    }
                        //throw setError(E_FAIL,
                        //               tr("Internal inconsistency looking up disk image '%s'. "
                        //                  "Check compliance OVA package structure and file names "
                        //                  "references in the section <References> in the OVF file."),
                        //               stack.pszOvaLookAheadName);

                    /* replace with a new found disk image */
                    diCurrent = *(&itDiskImage->second);

                    /*
                     * Again iterate over all given disk images of the virtual system
                     * disks description using the found disk image
                     */
                    vsdeTargetHD = NULL;
                    for (list<VirtualSystemDescriptionEntry*>::const_iterator itHD = avsdeHDs.begin();
                         itHD != avsdeHDs.end();
                         ++itHD)
                    {
                        VirtualSystemDescriptionEntry *vsdeHD = *itHD;
                        if (vsdeHD->strRef == diCurrent.strDiskId)
                        {
                            vsdeTargetHD = vsdeHD;
                            break;
                        }
                    }

                    /*
                     * in this case it's an error because something is wrong with the OVF description file.
                     * May be VBox imports OVA package with wrong file sequence inside the archive.
                     */
                    if (!vsdeTargetHD)
                        throw setError(E_FAIL,
                                       tr("Internal inconsistency looking up disk image '%s'"),
                                       diCurrent.strHref.c_str());
                }
                else
                {
                    ++oit;
                }
            }
            else
            {
                ++oit;
                continue;
            }
        }
        else
        {
            /* just continue with normal files*/
            ++oit;
        }

        /* Important! to store disk name for the next checks */
        disksResolvedNames.insert(diCurrent.strHref);
////// end of duplicated code.
        // there must be an image in the OVF disk structs with the same UUID
        bool fFound = false;
        Utf8Str strUuid;

        /*
         * Before importing the virtual hard disk found above (diCurrent/vsdeTargetHD) first
         * check if the user requested to change either the controller it is to be attached
         * to and/or the controller port (aka 'channel') on the controller.
         */
        if (   !vsdeTargetHD->strExtraConfigCurrent.isEmpty()
            && vsdeTargetHD->strExtraConfigSuggested != vsdeTargetHD->strExtraConfigCurrent)
        {
            /*
             * First, we examine the extra configuration values for this vdisk:
             *   vsdeTargetHD->strExtraConfigSuggested
             *   vsdeTargetHD->strExtraConfigCurrent
             * in order to extract both the "before" and "after" storage controller and port
             * details. The strExtraConfigSuggested string contains the current controller
             * and port the vdisk is attached to and is populated by Appliance::interpret()
             * when processing the OVF data; it is in the following format:
             * 'controller=12;channel=0' (the 'channel=' label for the controller port is
             * historical and is documented as such in the SDK so can't be changed). The
             * strExtraConfigSuggested string contains the target controller and port specified
             * by the user and it has the same format. The 'controller=' value is not a
             * controller-ID but rather it is the index for the corresponding storage controller
             * in the array of VirtualSystemDescriptionEntry entries.
             */
            int vrc;
            uint32_t uOrigControllerIndex;
            vrc = getStorageControllerDetailsFromStr(vsdeTargetHD->strExtraConfigSuggested, "controller=", &uOrigControllerIndex);
            if (RT_FAILURE(vrc))
                throw setError(E_FAIL,
                               tr("Original controller value invalid or missing: '%s'"),
                               vsdeTargetHD->strExtraConfigSuggested.c_str());

            uint32_t uTargetControllerIndex;
            vrc = getStorageControllerDetailsFromStr(vsdeTargetHD->strExtraConfigCurrent, "controller=", &uTargetControllerIndex);
            if (RT_FAILURE(vrc))
                throw setError(E_FAIL,
                               tr("Target controller value invalid or missing: '%s'"),
                               vsdeTargetHD->strExtraConfigCurrent.c_str());

            uint32_t uOrigControllerPortValue;
            vrc = getStorageControllerDetailsFromStr(vsdeTargetHD->strExtraConfigSuggested, "channel=",
                &uOrigControllerPortValue);
            if (RT_FAILURE(vrc))
                throw setError(E_FAIL,
                               tr("Original controller port ('channel=') invalid or missing: '%s'"),
                               vsdeTargetHD->strExtraConfigSuggested.c_str());

            uint32_t uNewControllerPortValue;
            vrc = getStorageControllerDetailsFromStr(vsdeTargetHD->strExtraConfigCurrent, "channel=", &uNewControllerPortValue);
            if (RT_FAILURE(vrc))
                throw setError(E_FAIL,
                               tr("Target controller port ('channel=') invalid or missing: '%s'"),
                               vsdeTargetHD->strExtraConfigCurrent.c_str());

            /*
             * Second, now that we have the storage controller indexes we locate the corresponding
             * VirtualSystemDescriptionEntry (VSDE) for both storage controllers which contain
             * identifying details which will be needed later when walking the list of storage
             * controllers.
             */
            const VirtualSystemDescriptionEntry *vsdeOrigController;
            vsdeOrigController = vsdescThis->i_findByIndex(uOrigControllerIndex);
            if (!vsdeOrigController)
                throw setError(E_FAIL,
                               tr("Failed to find storage controller '%u' in the System Description list"),
                               uOrigControllerIndex);

            const VirtualSystemDescriptionEntry *vsdeTargetController;
            vsdeTargetController = vsdescThis->i_findByIndex(uTargetControllerIndex);
            if (!vsdeTargetController)
                throw setError(E_FAIL,
                               tr("Failed to find storage controller '%u' in the System Description list"),
                               uTargetControllerIndex);

            /*
             * Third, grab the UUID of the current vdisk so we can identify which device
             * attached to the original storage controller needs to be updated (channel) and/or
             * removed.
             */
            ovf::DiskImagesMap::const_iterator itDiskImageMap = stack.mapDisks.find(vsdeTargetHD->strRef);
            if (itDiskImageMap == stack.mapDisks.end())
                throw setError(E_FAIL,
                               tr("Failed to find virtual disk '%s' in DiskImagesMap"),
                               vsdeTargetHD->strVBoxCurrent.c_str());
            const ovf::DiskImage &targetDiskImage = itDiskImageMap->second;
            Utf8Str strTargetDiskUuid = targetDiskImage.uuidVBox;;

            /*
             * Fourth, walk the attached devices of the original storage controller to find the
             * current vdisk and update the controller port (aka channel) value if necessary and
             * also remove the vdisk from this controller if needed.
             *
             * A short note on the choice of which items to compare when determining the type of
             * storage controller here and below in the vdisk addition scenario:
             *  + The VirtualSystemDescriptionEntry 'strOvf' field is populated from the OVF
             *    data which can contain a value like 'vmware.sata.ahci' if created by VMWare so
             *    it isn't a reliable choice.
             *  + The settings::StorageController 'strName' field can have varying content based
             *    on the version of the settings file, e.g. 'IDE Controller' vs. 'IDE' so it
             *    isn't a reliable choice.  Further, this field can contain 'SATA' whereas
             *    'AHCI' is used in 'strOvf' and 'strVBoxSuggested'.
             *  + The VirtualSystemDescriptionEntry 'strVBoxSuggested' field is populated by
             *    Appliance::interpret()->VirtualSystemDescription::i_addEntry() and is thus
             *    under VBox's control and has a fixed format and predictable content.
             */
            bool fDiskRemoved = false;
            settings::AttachedDevice originalAttachedDevice;
            settings::StorageControllersList::iterator itSCL;
            for (itSCL = config.hardwareMachine.storage.llStorageControllers.begin();
                 itSCL != config.hardwareMachine.storage.llStorageControllers.end();
                 ++itSCL)
            {
                settings::StorageController &SC = *itSCL;
                const char *pcszSCType = Global::stringifyStorageControllerType(SC.controllerType);

                /* There can only be one storage controller of each type in the OVF data. */
                if (!vsdeOrigController->strVBoxSuggested.compare(pcszSCType, Utf8Str::CaseInsensitive))
                {
                    settings::AttachedDevicesList::iterator itAD;
                    for (itAD = SC.llAttachedDevices.begin();
                         itAD != SC.llAttachedDevices.end();
                         ++itAD)
                    {
                        settings::AttachedDevice &AD = *itAD;

                        if (AD.uuid.toString() == strTargetDiskUuid)
                        {
                            ULONG ulMaxPorts;
                            hrc = i_verifyStorageControllerPortValid(SC.controllerType, uNewControllerPortValue, &ulMaxPorts);
                            if (FAILED(hrc))
                            {
                                if (hrc == E_INVALIDARG)
                                    throw setError(E_INVALIDARG,
                                                   tr("Illegal channel: '%u'.  For %s controllers the valid values are "
                                                   "0 to %lu (inclusive).\n"), uNewControllerPortValue, pcszSCType, ulMaxPorts-1);
                                else
                                    throw hrc;
                            }

                            if (uOrigControllerPortValue != uNewControllerPortValue)
                            {
                                AD.lPort = (int32_t)uNewControllerPortValue;
                            }
                            if (uOrigControllerIndex != uTargetControllerIndex)
                            {
                                LogFunc(("Removing vdisk '%s' (uuid = %RTuuid) from the %s storage controller.\n",
                                         vsdeTargetHD->strVBoxCurrent.c_str(),
                                         itAD->uuid.raw(),
                                         SC.strName.c_str()));
                                originalAttachedDevice = AD;
                                SC.llAttachedDevices.erase(itAD);
                                fDiskRemoved = true;
                            }
                        }
                    }
                }
            }

            /*
             * Fifth, if we are moving the vdisk to a different controller and not just changing
             * the channel then we walk the attached devices of the target controller and check
             * for conflicts before adding the vdisk detached/removed above.
             */
            bool fDiskAdded = false;
            if (fDiskRemoved)
            {
                for (itSCL = config.hardwareMachine.storage.llStorageControllers.begin();
                     itSCL != config.hardwareMachine.storage.llStorageControllers.end();
                     ++itSCL)
                {
                    settings::StorageController &SC = *itSCL;
                    const char *pcszSCType = Global::stringifyStorageControllerType(SC.controllerType);

                    /* There can only be one storage controller of each type in the OVF data. */
                    if (!vsdeTargetController->strVBoxSuggested.compare(pcszSCType, Utf8Str::CaseInsensitive))
                    {
                        settings::AttachedDevicesList::iterator itAD;
                        for (itAD = SC.llAttachedDevices.begin();
                             itAD != SC.llAttachedDevices.end();
                             ++itAD)
                        {
                            settings::AttachedDevice &AD = *itAD;
                            if (   AD.lDevice == originalAttachedDevice.lDevice
                                && AD.lPort == originalAttachedDevice.lPort)
                                    throw setError(E_FAIL,
                                                   tr("Device of type '%s' already attached to the %s controller at this "
                                                   "port/channel (%d)."),
                                                   Global::stringifyDeviceType(AD.deviceType), pcszSCType, AD.lPort);
                        }

                        LogFunc(("Adding vdisk '%s' (uuid = %RTuuid) to the %s storage controller\n",
                                 vsdeTargetHD->strVBoxCurrent.c_str(),
                                 originalAttachedDevice.uuid.raw(),
                                 SC.strName.c_str()));
                        SC.llAttachedDevices.push_back(originalAttachedDevice);
                        fDiskAdded = true;
                    }
                }

                if (!fDiskAdded)
                    throw setError(E_FAIL,
                                   tr("Failed to add disk '%s' (uuid=%RTuuid) to the %s storage controller."),
                                   vsdeTargetHD->strVBoxCurrent.c_str(),
                                   originalAttachedDevice.uuid.raw(),
                                   vsdeTargetController->strVBoxSuggested.c_str());
            }

            /*
             * Sixth, update the machine settings since we've changed the storage controller
             * and/or controller port for this vdisk.
             */
            AutoWriteLock vboxLock(mVirtualBox COMMA_LOCKVAL_SRC_POS);
            mVirtualBox->i_saveSettings();
            vboxLock.release();
        }

        // for each storage controller...
        for (settings::StorageControllersList::iterator sit = config.hardwareMachine.storage.llStorageControllers.begin();
             sit != config.hardwareMachine.storage.llStorageControllers.end();
             ++sit)
        {
            settings::StorageController &sc = *sit;

            // for each medium attachment to this controller...
            for (settings::AttachedDevicesList::iterator dit = sc.llAttachedDevices.begin();
                 dit != sc.llAttachedDevices.end();
                 ++dit)
            {
                settings::AttachedDevice &d = *dit;

                if (d.uuid.isZero())
                    // empty DVD and floppy media
                    continue;

                // When repairing a broken VirtualBox xml config section (written
                // by VirtualBox versions earlier than 3.2.10) assume the disks
                // show up in the same order as in the OVF description.
                if (fRepairDuplicate)
                {
                    VirtualSystemDescriptionEntry *vsdeHD = *avsdeHDsIt;
                    ovf::DiskImagesMap::const_iterator itDiskImage = stack.mapDisks.find(vsdeHD->strRef);
                    if (itDiskImage != stack.mapDisks.end())
                    {
                        const ovf::DiskImage &di = itDiskImage->second;
                        d.uuid = Guid(di.uuidVBox);
                    }
                    ++avsdeHDsIt;
                }

                // convert the Guid to string
                strUuid = d.uuid.toString();

                if (diCurrent.uuidVBox != strUuid)
                {
                    continue;
                }

                /*
                 * step 3: import disk
                 */
                ComObjPtr<Medium> pTargetMedium;
                i_importOneDiskImage(diCurrent,
                                     vsdeTargetHD->strVBoxCurrent,
                                     pTargetMedium,
                                     stack);

                // ... and replace the old UUID in the machine config with the one of
                // the imported disk that was just created
                Bstr hdId;
                hrc = pTargetMedium->COMGETTER(Id)(hdId.asOutParam());
                if (FAILED(hrc)) throw hrc;

                /*
                 * 1. saving original UUID for restoring in case of failure.
                 * 2. replacement of original UUID by new UUID in the current VM config (settings::MachineConfigFile).
                 */
                {
                    stack.saveOriginalUUIDOfAttachedDevice(d, Utf8Str(hdId));
                    d.uuid = hdId;
                }

                fFound = true;
                break;
            } // for (settings::AttachedDevicesList::const_iterator dit = sc.llAttachedDevices.begin();
        } // for (settings::StorageControllersList::const_iterator sit = config.hardwareMachine.storage.llStorageControllers.begin();

            // no disk with such a UUID found:
        if (!fFound)
            throw setError(E_FAIL,
                           tr("<vbox:Machine> element in OVF contains a medium attachment for the disk image %s "
                              "but the OVF describes no such image"),
                           strUuid.c_str());

        ++cImportedDisks;

    }// while(oit != stack.mapDisks.end())


    /*
     * quantity of the imported disks isn't equal to the size of the avsdeHDs list.
     */
    if(cImportedDisks < avsdeHDs.size())
    {
        Log1Warning(("Not all disk images were imported for VM %s. Check OVF description file.",
                     vmNameEntry->strOvf.c_str()));
    }

    /*
     * step 4): create the machine and have it import the config
     */

    ComObjPtr<Machine> pNewMachine;
    hrc = pNewMachine.createObject();
    if (FAILED(hrc)) throw hrc;

    // this magic constructor fills the new machine object with the MachineConfig
    // instance that we created from the vbox:Machine
    hrc = pNewMachine->init(mVirtualBox,
                            stack.strNameVBox,// name from OVF preparations; can be suffixed to avoid duplicates
                            stack.strSettingsFilename,
                            config);          // the whole machine config
    if (FAILED(hrc)) throw hrc;

    pReturnNewMachine = ComPtr<IMachine>(pNewMachine);

    // and register it
    hrc = mVirtualBox->RegisterMachine(pNewMachine);
    if (FAILED(hrc)) throw hrc;

    // store new machine for roll-back in case of errors
    Bstr bstrNewMachineId;
    hrc = pNewMachine->COMGETTER(Id)(bstrNewMachineId.asOutParam());
    if (FAILED(hrc)) throw hrc;
    m->llGuidsMachinesCreated.push_back(Guid(bstrNewMachineId));

    LogFlowFuncLeave();
}

/**
 * @throws HRESULT errors.
 */
void Appliance::i_importMachines(ImportStack &stack)
{
    // this is safe to access because this thread only gets started
    const ovf::OVFReader &reader = *m->pReader;

    // create a session for the machine + disks we manipulate below
    HRESULT hrc = stack.pSession.createInprocObject(CLSID_Session);
    ComAssertComRCThrowRC(hrc);

    list<ovf::VirtualSystem>::const_iterator it;
    list< ComObjPtr<VirtualSystemDescription> >::const_iterator it1;
    /* Iterate through all virtual systems of that appliance */
    size_t i = 0;
    for (it  = reader.m_llVirtualSystems.begin(), it1  = m->virtualSystemDescriptions.begin();
         it != reader.m_llVirtualSystems.end() && it1 != m->virtualSystemDescriptions.end();
         ++it, ++it1, ++i)
    {
        const ovf::VirtualSystem &vsysThis = *it;
        ComObjPtr<VirtualSystemDescription> vsdescThis = (*it1);

        // there are two ways in which we can create a vbox machine from OVF:
        // -- either this OVF was written by vbox 3.2 or later, in which case there is a <vbox:Machine> element
        //    in the <VirtualSystem>; then the VirtualSystemDescription::Data has a settings::MachineConfigFile
        //    with all the machine config pretty-parsed;
        // -- or this is an OVF from an older vbox or an external source, and then we need to translate the
        //    VirtualSystemDescriptionEntry and do import work

        // Even for the vbox:Machine case, there are a number of configuration items that will be taken from
        // the OVF because otherwise the "override import parameters" mechanism in the GUI won't work.

        // VM name
        std::list<VirtualSystemDescriptionEntry*> vsdeName = vsdescThis->i_findByType(VirtualSystemDescriptionType_Name);
        if (vsdeName.size() < 1)
            throw setError(VBOX_E_FILE_ERROR,
                           tr("Missing VM name"));
        stack.strNameVBox = vsdeName.front()->strVBoxCurrent;

        // Primary group, which is entirely optional.
        stack.strPrimaryGroup.setNull();
        std::list<VirtualSystemDescriptionEntry*> vsdePrimaryGroup = vsdescThis->i_findByType(VirtualSystemDescriptionType_PrimaryGroup);
        if (vsdePrimaryGroup.size() >= 1)
        {
            stack.strPrimaryGroup = vsdePrimaryGroup.front()->strVBoxCurrent;
            if (stack.strPrimaryGroup.isEmpty())
                stack.strPrimaryGroup = "/";
        }

        // Draw the right conclusions from the (possibly modified) VM settings
        // file name and base folder. If the VM settings file name is modified,
        // it takes precedence, otherwise it is recreated from the base folder
        // and the primary group.
        stack.strSettingsFilename.setNull();
        std::list<VirtualSystemDescriptionEntry*> vsdeSettingsFile = vsdescThis->i_findByType(VirtualSystemDescriptionType_SettingsFile);
        if (vsdeSettingsFile.size() >= 1)
        {
            VirtualSystemDescriptionEntry *vsdeSF1 = vsdeSettingsFile.front();
            if (vsdeSF1->strVBoxCurrent != vsdeSF1->strVBoxSuggested)
                stack.strSettingsFilename = vsdeSF1->strVBoxCurrent;
        }
        if (stack.strSettingsFilename.isEmpty())
        {
            Utf8Str strBaseFolder;
            std::list<VirtualSystemDescriptionEntry*> vsdeBaseFolder = vsdescThis->i_findByType(VirtualSystemDescriptionType_BaseFolder);
            if (vsdeBaseFolder.size() >= 1)
                strBaseFolder = vsdeBaseFolder.front()->strVBoxCurrent;
            Bstr bstrSettingsFilename;
            hrc = mVirtualBox->ComposeMachineFilename(Bstr(stack.strNameVBox).raw(),
                                                      Bstr(stack.strPrimaryGroup).raw(),
                                                      NULL /* aCreateFlags */,
                                                      Bstr(strBaseFolder).raw(),
                                                      bstrSettingsFilename.asOutParam());
            if (FAILED(hrc)) throw hrc;
            stack.strSettingsFilename = bstrSettingsFilename;
        }

        // Determine the machine folder from the settings file.
        LogFunc(("i=%zu strName=%s strSettingsFilename=%s\n", i, stack.strNameVBox.c_str(), stack.strSettingsFilename.c_str()));
        stack.strMachineFolder = stack.strSettingsFilename;
        stack.strMachineFolder.stripFilename();

        // guest OS type
        std::list<VirtualSystemDescriptionEntry*> vsdeOS;
        vsdeOS = vsdescThis->i_findByType(VirtualSystemDescriptionType_OS);
        if (vsdeOS.size() < 1)
            throw setError(VBOX_E_FILE_ERROR,
                           tr("Missing guest OS type"));
        stack.strOsTypeVBox = vsdeOS.front()->strVBoxCurrent;

        // Firmware
        std::list<VirtualSystemDescriptionEntry*> firmware = vsdescThis->i_findByType(VirtualSystemDescriptionType_BootingFirmware);
        if (firmware.size() != 1)
            stack.strFirmwareType = "BIOS";//try default BIOS type
        else
            stack.strFirmwareType = firmware.front()->strVBoxCurrent;

        // CPU count
        std::list<VirtualSystemDescriptionEntry*> vsdeCPU = vsdescThis->i_findByType(VirtualSystemDescriptionType_CPU);
        if (vsdeCPU.size() != 1)
            throw setError(VBOX_E_FILE_ERROR, tr("CPU count missing"));

        stack.cCPUs = vsdeCPU.front()->strVBoxCurrent.toUInt32();
        // We need HWVirt & IO-APIC if more than one CPU is requested
        if (stack.cCPUs > 1)
        {
            stack.fForceHWVirt = true;
            stack.fForceIOAPIC = true;
        }

        // RAM
        std::list<VirtualSystemDescriptionEntry*> vsdeRAM = vsdescThis->i_findByType(VirtualSystemDescriptionType_Memory);
        if (vsdeRAM.size() != 1)
            throw setError(VBOX_E_FILE_ERROR, tr("RAM size missing"));
        /* It's alway stored in bytes in VSD according to the old internal agreement within the team */
        uint64_t ullMemorySizeMB = vsdeRAM.front()->strVBoxCurrent.toUInt64() / _1M;
        stack.ulMemorySizeMB = (uint32_t)ullMemorySizeMB;

        // NVRAM file for VMs using EFI
        stack.strNvramPath.setNull();
        std::list<VirtualSystemDescriptionEntry*> vsdeNvram = vsdescThis->i_findByType(VirtualSystemDescriptionType_NVRAM);
        if (!vsdeNvram.empty())
            stack.strNvramPath = vsdeNvram.front()->strVBoxCurrent;

#ifdef VBOX_WITH_USB
        // USB controller
        std::list<VirtualSystemDescriptionEntry*> vsdeUSBController =
            vsdescThis->i_findByType(VirtualSystemDescriptionType_USBController);
        // USB support is enabled if there's at least one such entry; to disable USB support,
        // the type of the USB item would have been changed to "ignore"
        stack.fUSBEnabled = !vsdeUSBController.empty();
#endif
        // audio adapter
        std::list<VirtualSystemDescriptionEntry*> vsdeAudioAdapter =
            vsdescThis->i_findByType(VirtualSystemDescriptionType_SoundCard);
        /** @todo we support one audio adapter only */
        if (!vsdeAudioAdapter.empty())
            stack.strAudioAdapter = vsdeAudioAdapter.front()->strVBoxCurrent;

        // for the description of the new machine, always use the OVF entry, the user may have changed it in the import config
        std::list<VirtualSystemDescriptionEntry*> vsdeDescription =
            vsdescThis->i_findByType(VirtualSystemDescriptionType_Description);
        if (!vsdeDescription.empty())
            stack.strDescription = vsdeDescription.front()->strVBoxCurrent;

        // import vbox:machine or OVF now
        ComPtr<IMachine> pNewMachine; /** @todo pointless */
        if (vsdescThis->m->pConfig)
            // vbox:Machine config
            i_importVBoxMachine(vsdescThis, pNewMachine, stack);
        else
            // generic OVF config
            i_importMachineGeneric(vsysThis, vsdescThis, pNewMachine, stack);

    } // for (it = pAppliance->m->llVirtualSystems.begin() ...
}

void Appliance::ImportStack::saveOriginalUUIDOfAttachedDevice(settings::AttachedDevice &device,
                                                              const Utf8Str &newlyUuid)
{
    /* save for restoring */
    mapNewUUIDsToOriginalUUIDs.insert(std::make_pair(newlyUuid, device.uuid.toString()));
}

HRESULT Appliance::ImportStack::restoreOriginalUUIDOfAttachedDevice(settings::MachineConfigFile *config)
{
    settings::StorageControllersList &llControllers = config->hardwareMachine.storage.llStorageControllers;
    settings::StorageControllersList::iterator itscl;
    for (itscl = llControllers.begin();
         itscl != llControllers.end();
         ++itscl)
    {
        settings::AttachedDevicesList &llAttachments = itscl->llAttachedDevices;
        settings::AttachedDevicesList::iterator itadl = llAttachments.begin();
        while (itadl != llAttachments.end())
        {
            std::map<Utf8Str , Utf8Str>::iterator it =
                mapNewUUIDsToOriginalUUIDs.find(itadl->uuid.toString());
            if(it!=mapNewUUIDsToOriginalUUIDs.end())
            {
                Utf8Str uuidOriginal = it->second;
                itadl->uuid = Guid(uuidOriginal);
                mapNewUUIDsToOriginalUUIDs.erase(it->first);
            }
            ++itadl;
        }
    }

    return S_OK;
}

/**
 * @throws Nothing
 */
RTVFSIOSTREAM Appliance::ImportStack::claimOvaLookAHead(void)
{
    RTVFSIOSTREAM hVfsIos = this->hVfsIosOvaLookAhead;
    this->hVfsIosOvaLookAhead = NIL_RTVFSIOSTREAM;
    /* We don't free the name since it may be referenced in error messages and such. */
    return hVfsIos;
}

