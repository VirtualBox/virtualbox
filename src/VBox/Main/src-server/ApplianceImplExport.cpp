/* $Id: ApplianceImplExport.cpp 110133 2025-07-07 12:49:50Z brent.paulson@oracle.com $ */
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
#include <iprt/buildconfig.h>
#include <iprt/path.h>
#include <iprt/dir.h>
#include <iprt/param.h>
#include <iprt/manifest.h>
#include <iprt/stream.h>
#include <iprt/zip.h>

#include <iprt/formats/tar.h>

#include <VBox/version.h>

#include "ApplianceImpl.h"
#include "VirtualBoxImpl.h"
#include "ProgressImpl.h"
#include "MachineImpl.h"
#include "MediumImpl.h"
#include "LoggingNew.h"
#include "Global.h"
#include "MediumFormatImpl.h"
#include "SystemPropertiesImpl.h"

#include "AutoCaller.h"

#include "ApplianceImplPrivate.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
//
// IMachine public methods
//
////////////////////////////////////////////////////////////////////////////////

// This code is here so we won't have to include the appliance headers in the
// IMachine implementation, and we also need to access private appliance data.

/**
* Public method implementation.
* @param aAppliance     Appliance object.
* @param aLocation      Where to store the appliance.
* @param aDescription   Appliance description.
* @return
*/
HRESULT Machine::exportTo(const ComPtr<IAppliance> &aAppliance, const com::Utf8Str &aLocation,
                          ComPtr<IVirtualSystemDescription> &aDescription)
{
    HRESULT hrc = S_OK;

    if (!aAppliance)
        return E_POINTER;

    ComObjPtr<VirtualSystemDescription> pNewDesc;

    try
    {
        IAppliance *iAppliance = aAppliance;
        Appliance *pAppliance = static_cast<Appliance*>(iAppliance);

        LocationInfo locInfo;
        i_parseURI(aLocation, locInfo);

        Utf8Str strBasename(locInfo.strPath);
        strBasename.stripPath().stripSuffix();
        if (locInfo.strPath.endsWith(".tar.gz", Utf8Str::CaseSensitive))
            strBasename.stripSuffix();

        // create a new virtual system to store in the appliance
        hrc = pNewDesc.createObject();
        if (FAILED(hrc)) throw hrc;
        hrc = pNewDesc->init();
        if (FAILED(hrc)) throw hrc;

        // store the machine object so we can dump the XML in Appliance::Write()
        pNewDesc->m->pMachine = this;

#ifdef VBOX_WITH_USB
        // first, call the COM methods, as they request locks
        BOOL fUSBEnabled = FALSE;
        com::SafeIfaceArray<IUSBController> usbControllers;
        hrc = COMGETTER(USBControllers)(ComSafeArrayAsOutParam(usbControllers));
        if (SUCCEEDED(hrc))
        {
            for (unsigned i = 0; i < usbControllers.size(); ++i)
            {
                USBControllerType_T enmType;

                hrc = usbControllers[i]->COMGETTER(Type)(&enmType);
                if (FAILED(hrc)) throw hrc;

                if (enmType == USBControllerType_OHCI)
                    fUSBEnabled = TRUE;
            }
        }
#endif /* VBOX_WITH_USB */

        // request the machine lock while accessing internal members
        AutoReadLock alock1(this COMMA_LOCKVAL_SRC_POS);

        ComPtr<IAudioAdapter> pAudioAdapter;
        hrc = mAudioSettings->COMGETTER(Adapter)(pAudioAdapter.asOutParam());
        if (FAILED(hrc)) throw hrc;
        BOOL fAudioEnabled;
        hrc = pAudioAdapter->COMGETTER(Enabled)(&fAudioEnabled);
        if (FAILED(hrc)) throw hrc;
        AudioControllerType_T audioController;
        hrc = pAudioAdapter->COMGETTER(AudioController)(&audioController);
        if (FAILED(hrc)) throw hrc;

        // get name
        Utf8Str strVMName = mUserData->s.strName;
        // get description
        Utf8Str strDescription = mUserData->s.strDescription;
        // get guest OS
        Utf8Str strOsTypeVBox = mUserData->s.strOsType;
        // CPU count
        uint32_t cCPUs = mHWData->mCPUCount;
        // memory size in MB
        uint32_t ulMemSizeMB = mHWData->mMemorySize;

        PlatformArchitecture_T enmPlatformArch;
        mPlatform->COMGETTER(Architecture)(&enmPlatformArch);

        BOOL fLongMode = false;
        if (enmPlatformArch == PlatformArchitecture_x86)
        {
            ComPtr<IPlatformX86> pPlatformX86;
            mPlatform->COMGETTER(X86)(pPlatformX86.asOutParam());

            // VRAM size?
            // BIOS settings?
            // 3D acceleration enabled?
            // hardware virtualization enabled?
            // nested paging enabled?
            // HWVirtExVPIDEnabled?
            // PAEEnabled?
            // Long mode enabled?
            hrc = pPlatformX86->GetCPUProperty(CPUPropertyTypeX86_LongMode, &fLongMode);
            if (FAILED(hrc)) throw hrc;
        }

        // snapshotFolder?
        // VRDPServer?

        /* Guest OS type */
        ovf::CIMOSType_T cim = convertVBoxOSType2CIMOSType(strOsTypeVBox.c_str(), fLongMode);
        pNewDesc->i_addEntry(VirtualSystemDescriptionType_OS,
                             "",
                             Utf8StrFmt("%RI32", cim),
                             strOsTypeVBox);

        /* VM name */
        pNewDesc->i_addEntry(VirtualSystemDescriptionType_Name,
                             "",
                             strVMName,
                             strVMName);

        // description
        pNewDesc->i_addEntry(VirtualSystemDescriptionType_Description,
                             "",
                             strDescription,
                             strDescription);

        /* CPU count*/
        Utf8Str strCpuCount = Utf8StrFmt("%RI32", cCPUs);
        pNewDesc->i_addEntry(VirtualSystemDescriptionType_CPU,
                             "",
                             strCpuCount,
                             strCpuCount);

        /* Memory, it's alway stored in bytes in VSD according to the old internal agreement within the team */
        Utf8Str strMemory = Utf8StrFmt("%RI64", (uint64_t)ulMemSizeMB * _1M);
        pNewDesc->i_addEntry(VirtualSystemDescriptionType_Memory,
                             "",
                             strMemory,
                             strMemory);

        /* NVRAM file */
        FirmwareType_T enmFirmwareType = FirmwareType_BIOS;
        hrc = mFirmwareSettings->COMGETTER(FirmwareType)(&enmFirmwareType);
        if (FAILED(hrc)) throw hrc;

        if (enmFirmwareType != FirmwareType_BIOS)
        {
            Bstr bstrNonVolatilePath;
            hrc = mNvramStore->COMGETTER(NonVolatileStorageFile)(bstrNonVolatilePath.asOutParam());
            if (FAILED(hrc)) throw hrc;

            Utf8Str strNvramPath = bstrNonVolatilePath;
            if (strNvramPath.isNotEmpty() && RTFileExists(strNvramPath.c_str()))
            {
                /* The OVFValues[] array item here will be used for the 'ovf:href' attribute
                 * in the <File> child element of the <References> element and must be a
                 * relative pathname according to the OVF specification (section 7.1).
                 * The aVBoxValues[] array item is the absolute path to the source VM's
                 * NVRAM file which will be used for getting its size and accessing the file
                 * later. */
                Utf8Str strNvramOvfName = Utf8StrFmt("%s.nvram", strVMName.c_str());
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_NVRAM,
                                     "",
                                     strNvramOvfName, // aOvfValue - NVRAM filename based on target VM's name
                                     strNvramPath);   // aVBoxValue - absolute path to src VM's NVRAM file
            }
        }

        // the one VirtualBox IDE controller has two channels with two ports each, which is
        // considered two IDE controllers with two ports each by OVF, so export it as two
        int32_t lIDEControllerPrimaryIndex = 0;
        int32_t lIDEControllerSecondaryIndex = 0;
        int32_t lSATAControllerIndex = 0;
        int32_t lSCSIControllerIndex = 0;
        int32_t lVirtioSCSIControllerIndex = 0;
        int32_t lNVMeControllerIndex = 0;

        /* Fetch all available storage controllers */
        com::SafeIfaceArray<IStorageController> nwControllers;
        hrc = COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(nwControllers));
        if (FAILED(hrc)) throw hrc;

        ComPtr<IStorageController> pIDEController;
        ComPtr<IStorageController> pSATAController;
        ComPtr<IStorageController> pSCSIController;
        ComPtr<IStorageController> pSASController;
        ComPtr<IStorageController> pVirtioSCSIController;
        ComPtr<IStorageController> pNVMeController;
        for (size_t j = 0; j < nwControllers.size(); ++j)
        {
            StorageBus_T eType;
            hrc = nwControllers[j]->COMGETTER(Bus)(&eType);
            if (FAILED(hrc)) throw hrc;
            if (   eType == StorageBus_IDE
                && pIDEController.isNull())
                pIDEController = nwControllers[j];
            else if (   eType == StorageBus_SATA
                     && pSATAController.isNull())
                pSATAController = nwControllers[j];
            else if (   eType == StorageBus_SCSI
                     && pSCSIController.isNull())
                pSCSIController = nwControllers[j];
            else if (   eType == StorageBus_SAS
                     && pSASController.isNull())
                pSASController = nwControllers[j];
            else if (   eType == StorageBus_VirtioSCSI
                     && pVirtioSCSIController.isNull())
                pVirtioSCSIController = nwControllers[j];
            else if (   eType == StorageBus_PCIe
                     && pNVMeController.isNull())
                pNVMeController = nwControllers[j];
        }

//     <const name="HardDiskControllerIDE" value="6" />
        if (!pIDEController.isNull())
        {
            StorageControllerType_T ctlr;
            hrc = pIDEController->COMGETTER(ControllerType)(&ctlr);
            if (FAILED(hrc)) throw hrc;

            Utf8Str strVBox;
            switch (ctlr)
            {
                case StorageControllerType_PIIX3: strVBox = "PIIX3"; break;
                case StorageControllerType_PIIX4: strVBox = "PIIX4"; break;
                case StorageControllerType_ICH6: strVBox = "ICH6"; break;
                default: break; /* Shut up MSC. */
            }

            if (strVBox.length())
            {
                lIDEControllerPrimaryIndex = (int32_t)pNewDesc->m->maDescriptions.size();
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerIDE,
                                     Utf8StrFmt("%d", lIDEControllerPrimaryIndex),        // strRef
                                     strVBox,     // aOvfValue
                                     strVBox);    // aVBoxValue
                lIDEControllerSecondaryIndex = lIDEControllerPrimaryIndex + 1;
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerIDE,
                                     Utf8StrFmt("%d", lIDEControllerSecondaryIndex),
                                     strVBox,
                                     strVBox);
            }
        }

//     <const name="HardDiskControllerSATA" value="7" />
        if (!pSATAController.isNull())
        {
            Utf8Str strVBox = "AHCI";
            lSATAControllerIndex = (int32_t)pNewDesc->m->maDescriptions.size();
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerSATA,
                                 Utf8StrFmt("%d", lSATAControllerIndex),
                                 strVBox,
                                 strVBox);
        }

//     <const name="HardDiskControllerSCSI" value="8" />
        if (!pSCSIController.isNull())
        {
            StorageControllerType_T ctlr;
            hrc = pSCSIController->COMGETTER(ControllerType)(&ctlr);
            if (SUCCEEDED(hrc))
            {
                Utf8Str strVBox = "LsiLogic";       // the default in VBox
                switch (ctlr)
                {
                    case StorageControllerType_LsiLogic: strVBox = "LsiLogic"; break;
                    case StorageControllerType_BusLogic: strVBox = "BusLogic"; break;
                    default: break; /* Shut up MSC. */
                }
                lSCSIControllerIndex = (int32_t)pNewDesc->m->maDescriptions.size();
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerSCSI,
                                     Utf8StrFmt("%d", lSCSIControllerIndex),
                                     strVBox,
                                     strVBox);
            }
            else
                throw hrc;
        }

        if (!pSASController.isNull())
        {
            // VirtualBox considers the SAS controller a class of its own but in OVF
            // it should be a SCSI controller
            Utf8Str strVBox = "LsiLogicSas";
            lSCSIControllerIndex = (int32_t)pNewDesc->m->maDescriptions.size();
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerSAS,
                                 Utf8StrFmt("%d", lSCSIControllerIndex),
                                 strVBox,
                                 strVBox);
        }

        if (!pVirtioSCSIController.isNull())
        {
            StorageControllerType_T ctlr;
            hrc = pVirtioSCSIController->COMGETTER(ControllerType)(&ctlr);
            if (SUCCEEDED(hrc))
            {
                Utf8Str strVBox = "VirtioSCSI";       // the default in VBox
                switch (ctlr)
                {
                    case StorageControllerType_VirtioSCSI: strVBox = "VirtioSCSI"; break;
                    default: break; /* Shut up MSC. */
                }
                lVirtioSCSIControllerIndex = (int32_t)pNewDesc->m->maDescriptions.size();
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI,
                                     Utf8StrFmt("%d", lVirtioSCSIControllerIndex),
                                     strVBox,
                                     strVBox);
            }
            else
                throw hrc;
        }

        if (!pNVMeController.isNull())
        {
            Utf8Str strVBox = "NVMe";
            lNVMeControllerIndex = (int32_t)pNewDesc->m->maDescriptions.size();
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskControllerNVMe,
                                 Utf8StrFmt("%d", lNVMeControllerIndex),
                                 strVBox,
                                 strVBox);
        }

//     <const name="HardDiskImage" value="9" />
//     <const name="Floppy" value="18" />
//     <const name="CDROM" value="19" />

        for (MediumAttachmentList::const_iterator
             it = mMediumAttachments->begin();
             it != mMediumAttachments->end();
             ++it)
        {
            ComObjPtr<MediumAttachment> pHDA = *it;

            // the attachment's data
            ComPtr<IMedium> pMedium;
            ComPtr<IStorageController> ctl;
            Bstr controllerName;

            hrc = pHDA->COMGETTER(Controller)(controllerName.asOutParam());
            if (FAILED(hrc)) throw hrc;

            hrc = GetStorageControllerByName(controllerName.raw(), ctl.asOutParam());
            if (FAILED(hrc)) throw hrc;

            StorageBus_T storageBus;
            DeviceType_T deviceType;
            LONG lChannel;
            LONG lDevice;

            hrc = ctl->COMGETTER(Bus)(&storageBus);
            if (FAILED(hrc)) throw hrc;

            hrc = pHDA->COMGETTER(Type)(&deviceType);
            if (FAILED(hrc)) throw hrc;

            hrc = pHDA->COMGETTER(Port)(&lChannel);
            if (FAILED(hrc)) throw hrc;

            hrc = pHDA->COMGETTER(Device)(&lDevice);
            if (FAILED(hrc)) throw hrc;

            hrc = pHDA->COMGETTER(Medium)(pMedium.asOutParam());
            if (FAILED(hrc)) throw hrc;
            if (pMedium.isNull())
            {
                Utf8Str strStBus;
                if ( storageBus == StorageBus_IDE)
                    strStBus = "IDE";
                else if ( storageBus == StorageBus_SATA)
                    strStBus = "SATA";
                else if ( storageBus == StorageBus_SCSI)
                    strStBus = "SCSI";
                else if ( storageBus == StorageBus_SAS)
                    strStBus = "SAS";
                else if ( storageBus == StorageBus_PCIe)
                    strStBus = "PCIe";
                else if ( storageBus == StorageBus_VirtioSCSI)
                    strStBus = "VirtioSCSI";

                LogRel(("Warning: skip the medium (bus: %s, slot: %d, port: %d). No storage device attached.\n",
                strStBus.c_str(), lDevice, lChannel));
                continue;
            }

            Utf8Str strTargetImageName;
            Utf8Str strLocation;
            LONG64  llSize = 0;

            if (   deviceType == DeviceType_HardDisk
                && pMedium)
            {
                Bstr bstrLocation;

                hrc = pMedium->COMGETTER(Location)(bstrLocation.asOutParam());
                if (FAILED(hrc)) throw hrc;
                strLocation = bstrLocation;

                // find the source's base medium for two things:
                // 1) we'll use its name to determine the name of the target disk, which is readable,
                //    as opposed to the UUID filename of a differencing image, if pMedium is one
                // 2) we need the size of the base image so we can give it to addEntry(), and later
                //    on export, the progress will be based on that (and not the diff image)
                ComPtr<IMedium> pBaseMedium;
                hrc = pMedium->COMGETTER(Base)(pBaseMedium.asOutParam());
                        // returns pMedium if there are no diff images
                if (FAILED(hrc)) throw hrc;

                strTargetImageName = Utf8StrFmt("%s-disk%.3d.vmdk", strVMName.c_str(), ++pAppliance->m->cDisks);
                if (strTargetImageName.length() > RTZIPTAR_NAME_MAX)
                    throw setError(VBOX_E_NOT_SUPPORTED,
                                tr("Cannot attach disk '%s' -- file name too long"), strTargetImageName.c_str());

                // force reading state, or else size will be returned as 0
                MediumState_T ms;
                hrc = pBaseMedium->RefreshState(&ms);
                if (FAILED(hrc)) throw hrc;

                hrc = pBaseMedium->COMGETTER(Size)(&llSize);
                if (FAILED(hrc)) throw hrc;

                /* If the medium is encrypted add the key identifier to the list. */
                IMedium *iBaseMedium = pBaseMedium;
                Medium *pBase = static_cast<Medium*>(iBaseMedium);
                const com::Utf8Str strKeyId = pBase->i_getKeyId();
                if (!strKeyId.isEmpty())
                {
                    IMedium *iMedium = pMedium;
                    Medium *pMed = static_cast<Medium*>(iMedium);
                    com::Guid mediumUuid = pMed->i_getId();
                    bool fKnown = false;

                    /* Check whether the ID is already in our sequence, add it otherwise. */
                    for (unsigned i = 0; i < pAppliance->m->m_vecPasswordIdentifiers.size(); i++)
                    {
                        if (strKeyId.equals(pAppliance->m->m_vecPasswordIdentifiers[i]))
                        {
                            fKnown = true;
                            break;
                        }
                    }

                    if (!fKnown)
                    {
                        GUIDVEC vecMediumIds;

                        vecMediumIds.push_back(mediumUuid);
                        pAppliance->m->m_vecPasswordIdentifiers.push_back(strKeyId);
                        pAppliance->m->m_mapPwIdToMediumIds.insert(std::pair<com::Utf8Str, GUIDVEC>(strKeyId, vecMediumIds));
                    }
                    else
                    {
                        std::map<com::Utf8Str, GUIDVEC>::iterator itMap = pAppliance->m->m_mapPwIdToMediumIds.find(strKeyId);
                        if (itMap == pAppliance->m->m_mapPwIdToMediumIds.end())
                            throw setError(E_FAIL, tr("Internal error adding a medium UUID to the map"));
                        itMap->second.push_back(mediumUuid);
                    }
                }
            }
            else if (   deviceType == DeviceType_DVD
                     && pMedium)
            {
                /*
                 * check the minimal rules to grant access to export an image
                 * 1. no host drive CD/DVD image
                 * 2. the image must be accessible and readable
                 * 3. only ISO image is exported
                 */

                //1. no host drive CD/DVD image
                BOOL fHostDrive = false;
                hrc = pMedium->COMGETTER(HostDrive)(&fHostDrive);
                if (FAILED(hrc)) throw hrc;

                if(fHostDrive)
                    continue;

                //2. the image must be accessible and readable
                MediumState_T ms;
                hrc = pMedium->RefreshState(&ms);
                if (FAILED(hrc)) throw hrc;

                if (ms != MediumState_Created)
                    continue;

                //3. only ISO image is exported
                Bstr bstrLocation;
                hrc = pMedium->COMGETTER(Location)(bstrLocation.asOutParam());
                if (FAILED(hrc)) throw hrc;

                strLocation = bstrLocation;

                Utf8Str ext = strLocation;
                ext.assignEx(RTPathSuffix(strLocation.c_str()));//returns extension with dot (".iso")

                int eq = ext.compare(".iso", Utf8Str::CaseInsensitive);
                if (eq != 0)
                    continue;

                strTargetImageName = Utf8StrFmt("%s-disk%.3d.iso", strVMName.c_str(), ++pAppliance->m->cDisks);
                if (strTargetImageName.length() > RTZIPTAR_NAME_MAX)
                    throw setError(VBOX_E_NOT_SUPPORTED,
                                tr("Cannot attach image '%s' -- file name too long"), strTargetImageName.c_str());

                hrc = pMedium->COMGETTER(Size)(&llSize);
                if (FAILED(hrc)) throw hrc;
            }
            // and how this translates to the virtual system
            int32_t lControllerVsys = 0;
            LONG lChannelVsys;

            switch (storageBus)
            {
                case StorageBus_IDE:
                    // this is the exact reverse to what we're doing in Appliance::taskThreadImportMachines,
                    // and it must be updated when that is changed!
                    // Before 3.2 we exported one IDE controller with channel 0-3, but we now maintain
                    // compatibility with what VMware does and export two IDE controllers with two channels each

                    if (lChannel == 0 && lDevice == 0)      // primary master
                    {
                        lControllerVsys = lIDEControllerPrimaryIndex;
                        lChannelVsys = 0;
                    }
                    else if (lChannel == 0 && lDevice == 1) // primary slave
                    {
                        lControllerVsys = lIDEControllerPrimaryIndex;
                        lChannelVsys = 1;
                    }
                    else if (lChannel == 1 && lDevice == 0) // secondary master; by default this is the CD-ROM but
                                                            // as of VirtualBox 3.1 that can change
                    {
                        lControllerVsys = lIDEControllerSecondaryIndex;
                        lChannelVsys = 0;
                    }
                    else if (lChannel == 1 && lDevice == 1) // secondary slave
                    {
                        lControllerVsys = lIDEControllerSecondaryIndex;
                        lChannelVsys = 1;
                    }
                    else
                        throw setError(VBOX_E_NOT_SUPPORTED,
                                       tr("Cannot handle medium attachment: channel is %d, device is %d"), lChannel, lDevice);
                    break;

                case StorageBus_SATA:
                    lChannelVsys = lChannel;        // should be between 0 and 29
                    lControllerVsys = lSATAControllerIndex;
                    break;

                case StorageBus_VirtioSCSI:
                    lChannelVsys = lChannel;        // should be between 0 and 255
                    lControllerVsys = lVirtioSCSIControllerIndex;
                    break;

                case StorageBus_SCSI:
                case StorageBus_SAS:
                    lChannelVsys = lChannel;        // should be between 0 and 15
                    lControllerVsys = lSCSIControllerIndex;
                    break;

                case StorageBus_PCIe:
                    lChannelVsys = lChannel;        // should be between 0 and 255
                    lControllerVsys = lNVMeControllerIndex;
                    break;

                case StorageBus_Floppy:
                    lChannelVsys = 0;
                    lControllerVsys = 0;
                    break;

                default:
                    throw setError(VBOX_E_NOT_SUPPORTED,
                                   tr("Cannot handle medium attachment: storageBus is %d, channel is %d, device is %d"),
                                   storageBus, lChannel, lDevice);
            }

            Utf8StrFmt strExtra("controller=%RI32;channel=%RI32", lControllerVsys, lChannelVsys);
            Utf8Str strEmpty;

            switch (deviceType)
            {
                case DeviceType_HardDisk:
                    Log(("Adding VirtualSystemDescriptionType_HardDiskImage, disk size: %RI64\n", llSize));
                    pNewDesc->i_addEntry(VirtualSystemDescriptionType_HardDiskImage,
                                         strTargetImageName,   // disk ID: let's use the name
                                         strTargetImageName,   // OVF value:
                                         strLocation, // vbox value: media path
                                         (uint32_t)(llSize / _1M),
                                         strExtra);
                    break;

                case DeviceType_DVD:
                    Log(("Adding VirtualSystemDescriptionType_CDROM, disk size: %RI64\n", llSize));
                    pNewDesc->i_addEntry(VirtualSystemDescriptionType_CDROM,
                                         strTargetImageName,   // disk ID
                                         strTargetImageName,   // OVF value
                                         strLocation, // vbox value
                                         (uint32_t)(llSize / _1M),// ulSize
                                         strExtra);
                    break;

                case DeviceType_Floppy:
                    pNewDesc->i_addEntry(VirtualSystemDescriptionType_Floppy,
                                         strEmpty,      // disk ID
                                         strEmpty,      // OVF value
                                         strEmpty,      // vbox value
                                         1,       // ulSize
                                         strExtra);
                    break;

                default: break; /* Shut up MSC. */
            }
        }

//     <const name="NetworkAdapter" />
        ChipsetType_T enmChipsetType;
        hrc = mPlatform->getChipsetType(&enmChipsetType);
        if (FAILED(hrc))
            return hrc;

        uint32_t const maxNetworkAdapters = PlatformProperties::s_getMaxNetworkAdapters(enmChipsetType);
        size_t a;
        for (a = 0; a < maxNetworkAdapters; ++a)
        {
            ComPtr<INetworkAdapter> pNetworkAdapter;
            hrc = GetNetworkAdapter((ULONG)a, pNetworkAdapter.asOutParam());
            if (FAILED(hrc)) throw hrc;

            /* Enable the network card & set the adapter type */
            BOOL fEnabled;
            hrc = pNetworkAdapter->COMGETTER(Enabled)(&fEnabled);
            if (FAILED(hrc)) throw hrc;

            if (fEnabled)
            {
                NetworkAdapterType_T adapterType;
                hrc = pNetworkAdapter->COMGETTER(AdapterType)(&adapterType);
                if (FAILED(hrc)) throw hrc;

                NetworkAttachmentType_T attachmentType;
                hrc = pNetworkAdapter->COMGETTER(AttachmentType)(&attachmentType);
                if (FAILED(hrc)) throw hrc;

                Utf8Str strAttachmentType = convertNetworkAttachmentTypeToString(attachmentType);
                pNewDesc->i_addEntry(VirtualSystemDescriptionType_NetworkAdapter,
                                   "",      // ref
                                   strAttachmentType,      // orig
                                   Utf8StrFmt("%RI32", (uint32_t)adapterType),   // conf
                                   0,
                                   Utf8StrFmt("type=%s", strAttachmentType.c_str()));       // extra conf
            }
        }

//     <const name="USBController"  />
#ifdef VBOX_WITH_USB
        if (fUSBEnabled)
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_USBController, "", "", "");
#endif /* VBOX_WITH_USB */

//     <const name="SoundCard"  />
        if (fAudioEnabled)
            pNewDesc->i_addEntry(VirtualSystemDescriptionType_SoundCard,
                                 "",
                                 "ensoniq1371",       // this is what OVFTool writes and VMware supports
                                 Utf8StrFmt("%RI32", audioController));

        /* We return the new description to the caller */
        ComPtr<IVirtualSystemDescription> copy(pNewDesc);
        copy.queryInterfaceTo(aDescription.asOutParam());

        AutoWriteLock alock(pAppliance COMMA_LOCKVAL_SRC_POS);
        // finally, add the virtual system to the appliance
        pAppliance->m->virtualSystemDescriptions.push_back(pNewDesc);
    }
    catch(HRESULT arc)
    {
        hrc = arc;
    }

    return hrc;
}

////////////////////////////////////////////////////////////////////////////////
//
// IAppliance public methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Public method implementation.
 * @param aFormat   Appliance format.
 * @param aOptions  Export options.
 * @param aPath     Path to write the appliance to.
 * @param aProgress Progress object.
 * @return
 */
HRESULT Appliance::write(const com::Utf8Str &aFormat,
                         const std::vector<ExportOptions_T> &aOptions,
                         const com::Utf8Str &aPath,
                         ComPtr<IProgress> &aProgress)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->optListExport.clear();
    if (aOptions.size())
    {
        for (size_t i = 0; i < aOptions.size(); ++i)
        {
            m->optListExport.insert(i, aOptions[i]);
        }
    }

    HRESULT hrc = S_OK;
//  AssertReturn(!(m->optListExport.contains(ExportOptions_CreateManifest)
//  && m->optListExport.contains(ExportOptions_ExportDVDImages)), E_INVALIDARG);

    /* Parse all necessary info out of the URI */
    i_parseURI(aPath, m->locInfo);

    if (m->locInfo.storageType == VFSType_Cloud)
    {
        hrc = S_OK;
        ComObjPtr<Progress> progress;
        try
        {
            hrc = i_writeCloudImpl(m->locInfo, progress);
        }
        catch (HRESULT hrcXcpt)
        {
            hrc = hrcXcpt;
        }

        if (SUCCEEDED(hrc))
            /* Return progress to the caller */
            progress.queryInterfaceTo(aProgress.asOutParam());
    }
    else
    {
        m->fExportISOImages = m->optListExport.contains(ExportOptions_ExportDVDImages);

        if (!m->fExportISOImages)/* remove all ISO images from VirtualSystemDescription */
        {
            for (list<ComObjPtr<VirtualSystemDescription> >::const_iterator
                 it = m->virtualSystemDescriptions.begin();
                 it != m->virtualSystemDescriptions.end();
                 ++it)
            {
                ComObjPtr<VirtualSystemDescription> vsdescThis = *it;
                std::list<VirtualSystemDescriptionEntry*> skipped = vsdescThis->i_findByType(VirtualSystemDescriptionType_CDROM);
                std::list<VirtualSystemDescriptionEntry*>::const_iterator itSkipped = skipped.begin();
                while (itSkipped != skipped.end())
                {
                    (*itSkipped)->skipIt = true;
                    ++itSkipped;
                }
            }
        }

        // do not allow entering this method if the appliance is busy reading or writing
        if (!i_isApplianceIdle())
            return E_ACCESSDENIED;

        // figure the export format.  We exploit the unknown version value for oracle public cloud.
        ovf::OVFVersion_T ovfF;
        if (aFormat == "ovf-0.9")
            ovfF = ovf::OVFVersion_0_9;
        else if (aFormat == "ovf-1.0")
            ovfF = ovf::OVFVersion_1_0;
        else if (aFormat == "ovf-2.0")
            ovfF = ovf::OVFVersion_2_0;
        else if (aFormat == "opc-1.0")
            ovfF = ovf::OVFVersion_unknown;
        else
            return setError(VBOX_E_FILE_ERROR,
                            tr("Invalid format \"%s\" specified"), aFormat.c_str());

        // Check the extension.
        if (ovfF == ovf::OVFVersion_unknown)
        {
            if (!aPath.endsWith(".tar.gz", Utf8Str::CaseInsensitive))
                return setError(VBOX_E_FILE_ERROR,
                                tr("OPC appliance file must have .tar.gz extension"));
        }
        else if (   !aPath.endsWith(".ovf", Utf8Str::CaseInsensitive)
                 && !aPath.endsWith(".ova", Utf8Str::CaseInsensitive))
            return setError(VBOX_E_FILE_ERROR, tr("Appliance file must have .ovf or .ova extension"));


        /* As of OVF 2.0 we have to use SHA-256 in the manifest. */
        m->fManifest = m->optListExport.contains(ExportOptions_CreateManifest);
        if (m->fManifest)
            m->fDigestTypes = ovfF >= ovf::OVFVersion_2_0 ? RTMANIFEST_ATTR_SHA256 : RTMANIFEST_ATTR_SHA1;
        Assert(m->hOurManifest == NIL_RTMANIFEST);

        /* Check whether all passwords are supplied or error out. */
        if (m->m_cPwProvided < m->m_vecPasswordIdentifiers.size())
            return setError(VBOX_E_INVALID_OBJECT_STATE,
                            tr("Appliance export failed because not all passwords were provided for all encrypted media"));

        ComObjPtr<Progress> progress;
        hrc = S_OK;
        try
        {
            /* Parse all necessary info out of the URI */
            i_parseURI(aPath, m->locInfo);

            switch (ovfF)
            {
                case ovf::OVFVersion_unknown:
                    hrc = i_writeOPCImpl(ovfF, m->locInfo, progress);
                    break;
                default:
                    hrc = i_writeImpl(ovfF, m->locInfo, progress);
                    break;
            }

        }
        catch (HRESULT hrcXcpt)
        {
            hrc = hrcXcpt;
        }

        if (SUCCEEDED(hrc))
            /* Return progress to the caller */
            progress.queryInterfaceTo(aProgress.asOutParam());
    }

    return hrc;
}

////////////////////////////////////////////////////////////////////////////////
//
// Appliance private methods
//
////////////////////////////////////////////////////////////////////////////////

/*******************************************************************************
 * Export stuff
 ******************************************************************************/

/**
 * Implementation for writing out the OVF to disk. This starts a new thread which will call
 * Appliance::taskThreadWriteOVF().
 *
 * This is in a separate private method because it is used from two locations:
 *
 * 1) from the public Appliance::Write().
 *
 * 2) in a second worker thread; in that case, Appliance::Write() called Appliance::i_writeImpl(), which
 *    called Appliance::i_writeFSOVA(), which called Appliance::i_writeImpl(), which then called this again.
 *
 * @param aFormat
 * @param aLocInfo
 * @param aProgress
 * @return
 */
HRESULT Appliance::i_writeImpl(ovf::OVFVersion_T aFormat, const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress)
{
    /* Prepare progress object: */
    HRESULT hrc;
    try
    {
        hrc = i_setUpProgress(aProgress,
                              Utf8StrFmt(tr("Export appliance '%s'"), aLocInfo.strPath.c_str()),
                              aLocInfo.storageType == VFSType_File ? WriteFile : WriteS3);
    }
    catch (std::bad_alloc &) /* only Utf8StrFmt */
    {
        hrc = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hrc))
    {
        /* Create our worker task: */
        TaskOVF *pTask = NULL;
        try
        {
            pTask = new TaskOVF(this, TaskOVF::Write, aLocInfo, aProgress);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }

        /* The OVF version to produce: */
        pTask->enFormat = aFormat;

        /* Start the thread: */
        hrc = pTask->createThread();
        pTask = NULL;
    }
    return hrc;
}


HRESULT Appliance::i_writeCloudImpl(const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress)
{
    for (list<ComObjPtr<VirtualSystemDescription> >::const_iterator
         it = m->virtualSystemDescriptions.begin();
         it != m->virtualSystemDescriptions.end();
         ++it)
    {
        ComObjPtr<VirtualSystemDescription> vsdescThis = *it;
        std::list<VirtualSystemDescriptionEntry*> skipped = vsdescThis->i_findByType(VirtualSystemDescriptionType_CDROM);
        std::list<VirtualSystemDescriptionEntry*>::const_iterator itSkipped = skipped.begin();
        while (itSkipped != skipped.end())
        {
            (*itSkipped)->skipIt = true;
            ++itSkipped;
        }

        //remove all disks from the VirtualSystemDescription exept one
        skipped = vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskImage);
        itSkipped = skipped.begin();

        Utf8Str strBootLocation;
        while (itSkipped != skipped.end())
        {
            if (strBootLocation.isEmpty())
                strBootLocation = (*itSkipped)->strVBoxCurrent;
            else
                (*itSkipped)->skipIt = true;
            ++itSkipped;
        }

        //just in case
        if (vsdescThis->i_findByType(VirtualSystemDescriptionType_HardDiskImage).empty())
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("There are no images to export to Cloud after preparation steps"));

        /*
         * Fills out the OCI settings
         */
        std::list<VirtualSystemDescriptionEntry*> profileName
            = vsdescThis->i_findByType(VirtualSystemDescriptionType_CloudProfileName);
        if (profileName.size() > 1)
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Cloud: More than one profile name was found."));
        if (profileName.empty())
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Cloud: Profile name wasn't specified."));

        if (profileName.front()->strVBoxCurrent.isEmpty())
            return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Cloud: Cloud user profile name is empty"));

        LogRel(("profile name: %s\n", profileName.front()->strVBoxCurrent.c_str()));
    }

    // Create a progress object here otherwise Task won't be created successfully
    HRESULT hrc = aProgress.createObject();
    if (SUCCEEDED(hrc))
    {
        if (aLocInfo.strProvider.equals("OCI"))
            hrc = aProgress->init(mVirtualBox, static_cast<IAppliance *>(this),
                                  Utf8Str(tr("Exporting VM to Cloud...")),
                                  TRUE /* aCancelable */,
                                  5, // ULONG cOperations,
                                  1000, // ULONG ulTotalOperationsWeight,
                                  Utf8Str(tr("Exporting VM to Cloud...")), // aFirstOperationDescription
                                  10); // ULONG ulFirstOperationWeight
        else
            hrc = setError(VBOX_E_NOT_SUPPORTED,
                           tr("Only \"OCI\" cloud provider is supported for now. \"%s\" isn't supported."),
                           aLocInfo.strProvider.c_str());
        if (SUCCEEDED(hrc))
        {
            /* Initialize the worker task: */
            TaskCloud *pTask = NULL;
            try
            {
                pTask = new Appliance::TaskCloud(this, TaskCloud::Export, aLocInfo, aProgress);
            }
            catch (std::bad_alloc &)
            {
                pTask = NULL;
                hrc = E_OUTOFMEMORY;
            }
            if (SUCCEEDED(hrc))
            {
                /* Kick off the worker task: */
                hrc = pTask->createThread();
                pTask = NULL;
            }
        }
    }
    return hrc;
}

HRESULT Appliance::i_writeOPCImpl(ovf::OVFVersion_T aFormat, const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress)
{
    RT_NOREF(aFormat);

    /* Prepare progress object: */
    HRESULT hrc;
    try
    {
        hrc = i_setUpProgress(aProgress,
                              Utf8StrFmt(tr("Export appliance '%s'"), aLocInfo.strPath.c_str()),
                              aLocInfo.storageType == VFSType_File ? WriteFile : WriteS3);
    }
    catch (std::bad_alloc &) /* only Utf8StrFmt */
    {
        hrc = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hrc))
    {
        /* Create our worker task: */
        TaskOPC *pTask = NULL;
        try
        {
            pTask = new Appliance::TaskOPC(this, TaskOPC::Export, aLocInfo, aProgress);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }

        /* Kick it off: */
        hrc = pTask->createThread();
        pTask = NULL;
    }
    return hrc;
}


/**
 * Called from Appliance::i_writeFS() for creating a XML document for this
 * Appliance.
 *
 * @param writeLock                          The current write lock.
 * @param doc                                The xml document to fill.
 * @param stack                              Structure for temporary private
 *                                           data shared with caller.
 * @param enFormat                           OVF format (0.9 or 1.0).
 */
void Appliance::i_buildXML(AutoWriteLockBase& writeLock,
                           xml::Document &doc,
                           XMLStack &stack,
                           ovf::OVFVersion_T enFormat)
{
    xml::ElementNode *pelmRoot = doc.createRootElement("Envelope");

    pelmRoot->setAttribute("ovf:version", enFormat == ovf::OVFVersion_2_0 ? "2.0"
                                        : enFormat == ovf::OVFVersion_1_0 ? "1.0"
                                        :                       "0.9");
    pelmRoot->setAttribute("xml:lang", "en-US");

    Utf8Str strNamespace;

    if (enFormat == ovf::OVFVersion_0_9)
    {
        strNamespace = ovf::OVF09_URI_string;
    }
    else if (enFormat == ovf::OVFVersion_1_0)
    {
        strNamespace = ovf::OVF10_URI_string;
    }
    else
    {
        strNamespace = ovf::OVF20_URI_string;
    }

    pelmRoot->setAttribute("xmlns", strNamespace);
    pelmRoot->setAttribute("xmlns:ovf", strNamespace);

    //         pelmRoot->setAttribute("xmlns:ovfstr", "http://schema.dmtf.org/ovf/strings/1");
    pelmRoot->setAttribute("xmlns:rasd", "http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_ResourceAllocationSettingData");
    pelmRoot->setAttribute("xmlns:vssd", "http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_VirtualSystemSettingData");
    pelmRoot->setAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
    pelmRoot->setAttribute("xmlns:vbox", "http://www.virtualbox.org/ovf/machine");
    //         pelmRoot->setAttribute("xsi:schemaLocation", "http://schemas.dmtf.org/ovf/envelope/1 ../ovf-envelope.xsd");

    if (enFormat == ovf::OVFVersion_2_0)
    {
        pelmRoot->setAttribute("xmlns:epasd",
        "http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_EthernetPortAllocationSettingData.xsd");
        pelmRoot->setAttribute("xmlns:sasd",
        "http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_StorageAllocationSettingData.xsd");
    }

    // <Envelope>/<References>
    xml::ElementNode *pelmReferences = pelmRoot->createChild("References");     // 0.9 and 1.0

    /* <Envelope>/<DiskSection>:
       <DiskSection>
       <Info>List of the virtual disks used in the package</Info>
       <Disk ovf:capacity="4294967296" ovf:diskId="lamp" ovf:format="..." ovf:populatedSize="1924967692"/>
       </DiskSection> */
    xml::ElementNode *pelmDiskSection;
    if (enFormat == ovf::OVFVersion_0_9)
    {
        // <Section xsi:type="ovf:DiskSection_Type">
        pelmDiskSection = pelmRoot->createChild("Section");
        pelmDiskSection->setAttribute("xsi:type", "ovf:DiskSection_Type");
    }
    else
        pelmDiskSection = pelmRoot->createChild("DiskSection");

    xml::ElementNode *pelmDiskSectionInfo = pelmDiskSection->createChild("Info");
    pelmDiskSectionInfo->addContent("List of the virtual disks used in the package");

    /* <Envelope>/<NetworkSection>:
       <NetworkSection>
       <Info>Logical networks used in the package</Info>
       <Network ovf:name="VM Network">
       <Description>The network that the LAMP Service will be available on</Description>
       </Network>
       </NetworkSection> */
    xml::ElementNode *pelmNetworkSection;
    if (enFormat == ovf::OVFVersion_0_9)
    {
        // <Section xsi:type="ovf:NetworkSection_Type">
        pelmNetworkSection = pelmRoot->createChild("Section");
        pelmNetworkSection->setAttribute("xsi:type", "ovf:NetworkSection_Type");
    }
    else
        pelmNetworkSection = pelmRoot->createChild("NetworkSection");

    xml::ElementNode *pelmNetworkSectionInfo = pelmNetworkSection->createChild("Info");
    pelmNetworkSectionInfo->addContent("Logical networks used in the package");

    // and here come the virtual systems:

    // write a collection if we have more than one virtual system _and_ we're
    // writing OVF 1.0; otherwise fail since ovftool can't import more than
    // one machine, it seems
    xml::ElementNode *pelmToAddVirtualSystemsTo;
    if (m->virtualSystemDescriptions.size() > 1)
    {
        if (enFormat == ovf::OVFVersion_0_9)
            throw setError(VBOX_E_FILE_ERROR,
                           tr("Cannot export more than one virtual system with OVF 0.9, use OVF 1.0"));

        pelmToAddVirtualSystemsTo = pelmRoot->createChild("VirtualSystemCollection");
        pelmToAddVirtualSystemsTo->setAttribute("ovf:name", "ExportedVirtualBoxMachines");      // whatever
    }
    else
        pelmToAddVirtualSystemsTo = pelmRoot;       // add virtual system directly under root element

    // this list receives pointers to the XML elements in the machine XML which
    // might have UUIDs that need fixing after we know the UUIDs of the exported images
    std::list<xml::ElementNode*> llElementsWithUuidAttributes;
    uint32_t ulFile = 1;
    /* Iterate through all virtual systems of that appliance */
    for (list<ComObjPtr<VirtualSystemDescription> >::const_iterator
         itV = m->virtualSystemDescriptions.begin();
         itV != m->virtualSystemDescriptions.end();
         ++itV)
    {
        ComObjPtr<VirtualSystemDescription> vsdescThis = *itV;
        i_buildXMLForOneVirtualSystem(writeLock,
                                      *pelmToAddVirtualSystemsTo,
                                      &llElementsWithUuidAttributes,
                                      ulFile,
                                      vsdescThis,
                                      enFormat,
                                      stack);         // disks, networks, and files stack

        if (!stack.mapNvramFileForOneVM.empty())
        {
            /* This VM contains an NVRAM file so first we set up the <File> element's
             * details to be placed in the <References> element as described in section
             * 7.1 of the OVF specification using the information collected previously. */
            std::map<Utf8Str, std::pair<Utf8Str, Utf8Str> >::const_iterator
                it = stack.mapNvramFileForOneVM.begin();

            Utf8Str strFileRef = it->first; // e.g. file1
            Utf8Str strNvramSrcPath = it->second.first;
            Utf8Str strNvramTargetFile = it->second.second;

            uint64_t cbFile = 0;
            int vrc = RTFileQuerySizeByPath(strNvramSrcPath.c_str(), &cbFile);
            if (RT_FAILURE(vrc))
                throw setError(E_FAIL, tr("Failed to query size of NVRAM file '%s'"), strNvramSrcPath.c_str());

            /* Update the <References> element of the OVF file with the NVRAM file's
             * details using a <File> element containing the following details:
             * <File ovf:id="file1" ovf:href="vmname.nvram" ovf:size="1234"/> */
            xml::ElementNode *pelmFile = pelmReferences->createChild("File");
            pelmFile->setAttribute("ovf:id", strFileRef);
            pelmFile->setAttribute("ovf:href", strNvramTargetFile);
            pelmFile->setAttribute("ovf:size", Utf8StrFmt("%RI64", cbFile).c_str());

            stack.mapNvramFileForOneVM.clear();
        }

        for (list<Utf8Str>::const_iterator
             itDisk = stack.mapDiskSequenceForOneVM.begin();
             itDisk != stack.mapDiskSequenceForOneVM.end();
             ++itDisk)
        {
            const Utf8Str &strDiskID = *itDisk;
            const VirtualSystemDescriptionEntry *pDiskEntry = stack.mapDisks[strDiskID];

            // source path: where the VBox image is
            const Utf8Str &strSrcFilePath = pDiskEntry->strVBoxCurrent;
            Bstr bstrSrcFilePath(strSrcFilePath);

            //skip empty Medium. There are no information to add into section <References> or <DiskSection>
            if (strSrcFilePath.isEmpty() ||
                pDiskEntry->skipIt == true)
                continue;

            // Do NOT check here whether the file exists. FindMedium will figure
            // that out, and filesystem-based tests are simply wrong in the
            // general case (think of iSCSI).

            // We need some info from the source disks
            ComPtr<IMedium> pSourceDisk;
            //DeviceType_T deviceType = DeviceType_HardDisk;// by default

            Log(("Finding source disk \"%ls\"\n", bstrSrcFilePath.raw()));

            HRESULT hrc;

            if (pDiskEntry->type == VirtualSystemDescriptionType_HardDiskImage)
            {
                hrc = mVirtualBox->OpenMedium(bstrSrcFilePath.raw(),
                                              DeviceType_HardDisk,
                                              AccessMode_ReadWrite,
                                              FALSE /* fForceNewUuid */,
                                              pSourceDisk.asOutParam());
                if (FAILED(hrc))
                    throw hrc;
            }
            else if (pDiskEntry->type == VirtualSystemDescriptionType_CDROM)//may be, this is CD/DVD
            {
                hrc = mVirtualBox->OpenMedium(bstrSrcFilePath.raw(),
                                              DeviceType_DVD,
                                              AccessMode_ReadOnly,
                                              FALSE,
                                              pSourceDisk.asOutParam());
                if (FAILED(hrc))
                    throw hrc;
            }

            Bstr uuidSource;
            hrc = pSourceDisk->COMGETTER(Id)(uuidSource.asOutParam());
            if (FAILED(hrc)) throw hrc;
            Guid guidSource(uuidSource);

            // output filename
            const Utf8Str &strTargetFileNameOnly = pDiskEntry->strOvf;

            // We are always exporting to VMDK stream optimized for now
            //Bstr bstrSrcFormat = L"VMDK";//not used

            LONG64 cbCapacity = 0;     // size reported to guest
            hrc = pSourceDisk->COMGETTER(LogicalSize)(&cbCapacity);
            if (FAILED(hrc)) throw hrc;
            /// @todo r=poetzsch: wrong it is reported in bytes ...
            // capacity is reported in megabytes, so...
            //cbCapacity *= _1M;

            Guid guidTarget; /* Creates a new uniq number for the target disk. */
            guidTarget.create();

            // now handle the XML for the disk:
            Utf8StrFmt strFileRef("file%RI32", ulFile++);
            // <File ovf:href="WindowsXpProfessional-disk1.vmdk" ovf:id="file1" ovf:size="1710381056"/>
            xml::ElementNode *pelmFile = pelmReferences->createChild("File");
            pelmFile->setAttribute("ovf:id", strFileRef);
            pelmFile->setAttribute("ovf:href", strTargetFileNameOnly);
            /// @todo the actual size is not available at this point of time,
            // cause the disk will be compressed. The 1.0 standard says this is
            // optional! 1.1 isn't fully clear if the "gzip" format is used.
            // Need to be checked. */
            //            pelmFile->setAttribute("ovf:size", Utf8StrFmt("%RI64", cbFile).c_str());

            // add disk to XML Disks section
            // <Disk ovf:capacity="8589934592" ovf:diskId="vmdisk1" ovf:fileRef="file1" ovf:format="..."/>
            xml::ElementNode *pelmDisk = pelmDiskSection->createChild("Disk");
            pelmDisk->setAttribute("ovf:capacity", Utf8StrFmt("%RI64", cbCapacity).c_str());
            pelmDisk->setAttribute("ovf:diskId", strDiskID);
            pelmDisk->setAttribute("ovf:fileRef", strFileRef);

            if (pDiskEntry->type == VirtualSystemDescriptionType_HardDiskImage)//deviceType == DeviceType_HardDisk
            {
                pelmDisk->setAttribute("ovf:format",
                                       (enFormat == ovf::OVFVersion_0_9)
                                       ?  "http://www.vmware.com/specifications/vmdk.html#sparse"      // must be sparse or ovftoo
                                       :  "http://www.vmware.com/interfaces/specifications/vmdk.html#streamOptimized"
                                       // correct string as communicated to us by VMware (public bug #6612)
                                      );
            }
            else //pDiskEntry->type == VirtualSystemDescriptionType_CDROM, deviceType == DeviceType_DVD
            {
                pelmDisk->setAttribute("ovf:format",
                                       "http://www.ecma-international.org/publications/standards/Ecma-119.htm"
                                      );
            }

            // add the UUID of the newly target image to the OVF disk element, but in the
            // vbox: namespace since it's not part of the standard
            pelmDisk->setAttribute("vbox:uuid", Utf8StrFmt("%RTuuid", guidTarget.raw()).c_str());

            // now, we might have other XML elements from vbox:Machine pointing to this image,
            // but those would refer to the UUID of the _source_ image (which we created the
            // export image from); those UUIDs need to be fixed to the export image
            Utf8Str strGuidSourceCurly = guidSource.toStringCurly();
            for (std::list<xml::ElementNode*>::const_iterator
                 it = llElementsWithUuidAttributes.begin();
                 it != llElementsWithUuidAttributes.end();
                 ++it)
            {
                xml::ElementNode *pelmImage = *it;
                Utf8Str strUUID;
                pelmImage->getAttributeValue("uuid", strUUID);
                if (strUUID == strGuidSourceCurly)
                    // overwrite existing uuid attribute
                    pelmImage->setAttribute("uuid", guidTarget.toStringCurly());
            }
        }
        llElementsWithUuidAttributes.clear();
        stack.mapDiskSequenceForOneVM.clear();
    }

    // now, fill in the network section we set up empty above according
    // to the networks we found with the hardware items
    for (map<Utf8Str, bool>::const_iterator
         it = stack.mapNetworks.begin();
         it != stack.mapNetworks.end();
         ++it)
    {
        const Utf8Str &strNetwork = it->first;
        xml::ElementNode *pelmNetwork = pelmNetworkSection->createChild("Network");
        pelmNetwork->setAttribute("ovf:name", strNetwork.c_str());
        pelmNetwork->createChild("Description")->addContent("Logical network used by this appliance.");
    }
}

/**
 * Called from Appliance::i_buildXML() for each virtual system (machine) that
 * needs XML written out.
 *
 * @param writeLock                          The current write lock.
 * @param elmToAddVirtualSystemsTo           XML element to append elements to.
 * @param pllElementsWithUuidAttributes out: list of XML elements produced here
 *                                           with UUID attributes for quick
 *                                           fixing by caller later
 * @param ulFile                             The index for a 'File' element in the 'References' section.
 * @param vsdescThis                         The IVirtualSystemDescription
 *                                           instance for which to write XML.
 * @param enFormat                           OVF format (0.9 or 1.0).
 * @param stack                              Structure for temporary private
 *                                           data shared with caller.
 */
void Appliance::i_buildXMLForOneVirtualSystem(AutoWriteLockBase& writeLock,
                                              xml::ElementNode &elmToAddVirtualSystemsTo,
                                              std::list<xml::ElementNode*> *pllElementsWithUuidAttributes,
                                              uint32_t &ulFile,
                                              ComObjPtr<VirtualSystemDescription> &vsdescThis,
                                              ovf::OVFVersion_T enFormat,
                                              XMLStack &stack)
{
    LogFlowFunc(("ENTER appliance %p\n", this));

    xml::ElementNode *pelmVirtualSystem;
    if (enFormat == ovf::OVFVersion_0_9)
    {
        // <Section xsi:type="ovf:NetworkSection_Type">
        pelmVirtualSystem = elmToAddVirtualSystemsTo.createChild("Content");
        pelmVirtualSystem->setAttribute("xsi:type", "ovf:VirtualSystem_Type");
    }
    else
        pelmVirtualSystem = elmToAddVirtualSystemsTo.createChild("VirtualSystem");

    /*xml::ElementNode *pelmVirtualSystemInfo =*/ pelmVirtualSystem->createChild("Info")->addContent("A virtual machine");

    std::list<VirtualSystemDescriptionEntry*> llName = vsdescThis->i_findByType(VirtualSystemDescriptionType_Name);
    if (llName.empty())
        throw setError(VBOX_E_NOT_SUPPORTED, tr("Missing VM name"));
    Utf8Str &strVMName = llName.back()->strVBoxCurrent;
    pelmVirtualSystem->setAttribute("ovf:id", strVMName);

    // product info
    std::list<VirtualSystemDescriptionEntry*> llProduct    = vsdescThis->i_findByType(VirtualSystemDescriptionType_Product);
    std::list<VirtualSystemDescriptionEntry*> llProductUrl = vsdescThis->i_findByType(VirtualSystemDescriptionType_ProductUrl);
    std::list<VirtualSystemDescriptionEntry*> llVendor     = vsdescThis->i_findByType(VirtualSystemDescriptionType_Vendor);
    std::list<VirtualSystemDescriptionEntry*> llVendorUrl  = vsdescThis->i_findByType(VirtualSystemDescriptionType_VendorUrl);
    std::list<VirtualSystemDescriptionEntry*> llVersion    = vsdescThis->i_findByType(VirtualSystemDescriptionType_Version);
    bool fProduct    = llProduct.size()    && !llProduct.back()->strVBoxCurrent.isEmpty();
    bool fProductUrl = llProductUrl.size() && !llProductUrl.back()->strVBoxCurrent.isEmpty();
    bool fVendor     = llVendor.size()     && !llVendor.back()->strVBoxCurrent.isEmpty();
    bool fVendorUrl  = llVendorUrl.size()  && !llVendorUrl.back()->strVBoxCurrent.isEmpty();
    bool fVersion    = llVersion.size()    && !llVersion.back()->strVBoxCurrent.isEmpty();
    if (fProduct || fProductUrl || fVendor || fVendorUrl || fVersion)
    {
        /* <Section ovf:required="false" xsi:type="ovf:ProductSection_Type">
            <Info>Meta-information about the installed software</Info>
            <Product>VAtest</Product>
            <Vendor>SUN Microsystems</Vendor>
            <Version>10.0</Version>
            <ProductUrl>http://blogs.sun.com/VirtualGuru</ProductUrl>
            <VendorUrl>http://www.sun.com</VendorUrl>
        </Section> */
        xml::ElementNode *pelmAnnotationSection;
        if (enFormat == ovf::OVFVersion_0_9)
        {
            // <Section ovf:required="false" xsi:type="ovf:ProductSection_Type">
            pelmAnnotationSection = pelmVirtualSystem->createChild("Section");
            pelmAnnotationSection->setAttribute("xsi:type", "ovf:ProductSection_Type");
        }
        else
            pelmAnnotationSection = pelmVirtualSystem->createChild("ProductSection");

        pelmAnnotationSection->createChild("Info")->addContent("Meta-information about the installed software");
        if (fProduct)
            pelmAnnotationSection->createChild("Product")->addContent(llProduct.back()->strVBoxCurrent);
        if (fVendor)
            pelmAnnotationSection->createChild("Vendor")->addContent(llVendor.back()->strVBoxCurrent);
        if (fVersion)
            pelmAnnotationSection->createChild("Version")->addContent(llVersion.back()->strVBoxCurrent);
        if (fProductUrl)
            pelmAnnotationSection->createChild("ProductUrl")->addContent(llProductUrl.back()->strVBoxCurrent);
        if (fVendorUrl)
            pelmAnnotationSection->createChild("VendorUrl")->addContent(llVendorUrl.back()->strVBoxCurrent);
    }

    // description
    std::list<VirtualSystemDescriptionEntry*> llDescription = vsdescThis->i_findByType(VirtualSystemDescriptionType_Description);
    if (llDescription.size() &&
        !llDescription.back()->strVBoxCurrent.isEmpty())
    {
        /*  <Section ovf:required="false" xsi:type="ovf:AnnotationSection_Type">
                <Info>A human-readable annotation</Info>
                <Annotation>Plan 9</Annotation>
            </Section> */
        xml::ElementNode *pelmAnnotationSection;
        if (enFormat == ovf::OVFVersion_0_9)
        {
            // <Section ovf:required="false" xsi:type="ovf:AnnotationSection_Type">
            pelmAnnotationSection = pelmVirtualSystem->createChild("Section");
            pelmAnnotationSection->setAttribute("xsi:type", "ovf:AnnotationSection_Type");
        }
        else
            pelmAnnotationSection = pelmVirtualSystem->createChild("AnnotationSection");

        pelmAnnotationSection->createChild("Info")->addContent("A human-readable annotation");
        pelmAnnotationSection->createChild("Annotation")->addContent(llDescription.back()->strVBoxCurrent);
    }

    // license
    std::list<VirtualSystemDescriptionEntry*> llLicense = vsdescThis->i_findByType(VirtualSystemDescriptionType_License);
    if (llLicense.size() &&
        !llLicense.back()->strVBoxCurrent.isEmpty())
    {
        /* <EulaSection>
            <Info ovf:msgid="6">License agreement for the Virtual System.</Info>
            <License ovf:msgid="1">License terms can go in here.</License>
            </EulaSection> */
        xml::ElementNode *pelmEulaSection;
        if (enFormat == ovf::OVFVersion_0_9)
        {
            pelmEulaSection = pelmVirtualSystem->createChild("Section");
            pelmEulaSection->setAttribute("xsi:type", "ovf:EulaSection_Type");
        }
        else
            pelmEulaSection = pelmVirtualSystem->createChild("EulaSection");

        pelmEulaSection->createChild("Info")->addContent("License agreement for the virtual system");
        pelmEulaSection->createChild("License")->addContent(llLicense.back()->strVBoxCurrent);
    }

    // operating system
    std::list<VirtualSystemDescriptionEntry*> llOS = vsdescThis->i_findByType(VirtualSystemDescriptionType_OS);
    if (llOS.empty())
        throw setError(VBOX_E_NOT_SUPPORTED, tr("Missing OS type"));
    /*  <OperatingSystemSection ovf:id="82">
            <Info>Guest Operating System</Info>
            <Description>Linux 2.6.x</Description>
        </OperatingSystemSection> */
    VirtualSystemDescriptionEntry *pvsdeOS = llOS.back();
    xml::ElementNode *pelmOperatingSystemSection;
    if (enFormat == ovf::OVFVersion_0_9)
    {
        pelmOperatingSystemSection = pelmVirtualSystem->createChild("Section");
        pelmOperatingSystemSection->setAttribute("xsi:type", "ovf:OperatingSystemSection_Type");
    }
    else
        pelmOperatingSystemSection = pelmVirtualSystem->createChild("OperatingSystemSection");

    pelmOperatingSystemSection->setAttribute("ovf:id", pvsdeOS->strOvf);
    pelmOperatingSystemSection->createChild("Info")->addContent("The kind of installed guest operating system");
    Utf8Str strOSDesc;
    convertCIMOSType2VBoxOSType(strOSDesc, (ovf::CIMOSType_T)pvsdeOS->strOvf.toInt32(), "");
    pelmOperatingSystemSection->createChild("Description")->addContent(strOSDesc);
    // add the VirtualBox ostype in a custom tag in a different namespace
    xml::ElementNode *pelmVBoxOSType = pelmOperatingSystemSection->createChild("vbox:OSType");
    pelmVBoxOSType->setAttribute("ovf:required", "false");
    pelmVBoxOSType->addContent(pvsdeOS->strVBoxCurrent);

    // <VirtualHardwareSection ovf:id="hw1" ovf:transport="iso">
    xml::ElementNode *pelmVirtualHardwareSection;
    if (enFormat == ovf::OVFVersion_0_9)
    {
        // <Section xsi:type="ovf:VirtualHardwareSection_Type">
        pelmVirtualHardwareSection = pelmVirtualSystem->createChild("Section");
        pelmVirtualHardwareSection->setAttribute("xsi:type", "ovf:VirtualHardwareSection_Type");
    }
    else
        pelmVirtualHardwareSection = pelmVirtualSystem->createChild("VirtualHardwareSection");

    pelmVirtualHardwareSection->createChild("Info")->addContent("Virtual hardware requirements for a virtual machine");

    /*  <System>
            <vssd:Description>Description of the virtual hardware section.</vssd:Description>
            <vssd:ElementName>vmware</vssd:ElementName>
            <vssd:InstanceID>1</vssd:InstanceID>
            <vssd:VirtualSystemIdentifier>MyLampService</vssd:VirtualSystemIdentifier>
            <vssd:VirtualSystemType>vmx-4</vssd:VirtualSystemType>
        </System> */
    xml::ElementNode *pelmSystem = pelmVirtualHardwareSection->createChild("System");

    pelmSystem->createChild("vssd:ElementName")->addContent("Virtual Hardware Family"); // required OVF 1.0

    // <vssd:InstanceId>0</vssd:InstanceId>
    if (enFormat == ovf::OVFVersion_0_9)
        pelmSystem->createChild("vssd:InstanceId")->addContent("0");
    else // capitalization changed...
        pelmSystem->createChild("vssd:InstanceID")->addContent("0");

    // <vssd:VirtualSystemIdentifier>VAtest</vssd:VirtualSystemIdentifier>
    pelmSystem->createChild("vssd:VirtualSystemIdentifier")->addContent(strVMName);
    // <vssd:VirtualSystemType>vmx-4</vssd:VirtualSystemType>
    const char *pcszHardware = "virtualbox-2.2";
    if (enFormat == ovf::OVFVersion_0_9)
        // pretend to be vmware compatible then
        pcszHardware = "vmx-6";
    pelmSystem->createChild("vssd:VirtualSystemType")->addContent(pcszHardware);

    // loop thru all description entries twice; once to write out all
    // devices _except_ disk images, and a second time to assign the
    // disk images; this is because disk images need to reference
    // IDE controllers, and we can't know their instance IDs without
    // assigning them first

    uint32_t idIDEPrimaryController = 0;
    int32_t lIDEPrimaryControllerIndex = 0;
    uint32_t idIDESecondaryController = 0;
    int32_t lIDESecondaryControllerIndex = 0;
    uint32_t idSATAController = 0;
    int32_t lSATAControllerIndex = 0;
    uint32_t idSCSIController = 0;
    int32_t lSCSIControllerIndex = 0;
    uint32_t idVirtioSCSIController = 0;
    int32_t lVirtioSCSIControllerIndex = 0;
    uint32_t idNVMeController = 0;
    int32_t lNVMeControllerIndex = 0;

    uint32_t ulInstanceID = 1;

    uint32_t cDVDs = 0;

    for (size_t uLoop = 1; uLoop <= 2; ++uLoop)
    {
        int32_t lIndexThis = 0;
        for (vector<VirtualSystemDescriptionEntry>::const_iterator
             it = vsdescThis->m->maDescriptions.begin();
             it != vsdescThis->m->maDescriptions.end();
             ++it, ++lIndexThis)
        {
            const VirtualSystemDescriptionEntry &desc = *it;

            LogFlowFunc(("Loop %u: handling description entry ulIndex=%u, type=%s, strRef=%s, strOvf=%s, strVBox=%s, strExtraConfig=%s\n",
                         uLoop,
                         desc.ulIndex,
                         (  desc.type == VirtualSystemDescriptionType_HardDiskControllerIDE ? "HardDiskControllerIDE"
                          : desc.type == VirtualSystemDescriptionType_HardDiskControllerSATA ? "HardDiskControllerSATA"
                          : desc.type == VirtualSystemDescriptionType_HardDiskControllerSCSI ? "HardDiskControllerSCSI"
                          : desc.type == VirtualSystemDescriptionType_HardDiskControllerSAS ? "HardDiskControllerSAS"
                          : desc.type == VirtualSystemDescriptionType_HardDiskControllerNVMe ? "HardDiskControllerNVMe"
                          : desc.type == VirtualSystemDescriptionType_HardDiskImage ? "HardDiskImage"
                          : Utf8StrFmt("%d", desc.type).c_str()),
                         desc.strRef.c_str(),
                         desc.strOvf.c_str(),
                         desc.strVBoxCurrent.c_str(),
                         desc.strExtraConfigCurrent.c_str()));

            ovf::ResourceType_T type = (ovf::ResourceType_T)0;      // if this becomes != 0 then we do stuff
            Utf8Str strResourceSubType;

            Utf8Str strDescription;                             // results in <rasd:Description>...</rasd:Description> block
            Utf8Str strCaption;                                 // results in <rasd:Caption>...</rasd:Caption> block

            uint32_t ulParent = 0;

            int32_t lVirtualQuantity = -1;
            Utf8Str strAllocationUnits;

            int32_t lAddress = -1;
            int32_t lBusNumber = -1;
            int32_t lAddressOnParent = -1;

            int32_t lAutomaticAllocation = -1;                  // 0 means "false", 1 means "true"
            Utf8Str strConnection;                              // results in <rasd:Connection>...</rasd:Connection> block
            Utf8Str strHostResource;

            uint64_t uTemp;

            ovf::VirtualHardwareItem vhi;
            ovf::StorageItem si;
            ovf::EthernetPortItem epi;

            switch (desc.type)
            {
                case VirtualSystemDescriptionType_CPU:
                    /*  <Item>
                            <rasd:Caption>1 virtual CPU</rasd:Caption>
                            <rasd:Description>Number of virtual CPUs</rasd:Description>
                            <rasd:ElementName>virtual CPU</rasd:ElementName>
                            <rasd:InstanceID>1</rasd:InstanceID>
                            <rasd:ResourceType>3</rasd:ResourceType>
                            <rasd:VirtualQuantity>1</rasd:VirtualQuantity>
                        </Item> */
                    if (uLoop == 1)
                    {
                        strDescription = "Number of virtual CPUs";
                        type = ovf::ResourceType_Processor; // 3
                        desc.strVBoxCurrent.toInt(uTemp);
                        lVirtualQuantity = (int32_t)uTemp;
                        // without this ovftool won't eat the item
                        strCaption = Utf8StrFmt("%d virtual CPU%s", lVirtualQuantity, lVirtualQuantity > 1 ? "s" : "");
                    }
                break;

                case VirtualSystemDescriptionType_Memory:
                    /*  <Item>
                            <rasd:AllocationUnits>MegaBytes</rasd:AllocationUnits>
                            <rasd:Caption>256 MB of memory</rasd:Caption>
                            <rasd:Description>Memory Size</rasd:Description>
                            <rasd:ElementName>Memory</rasd:ElementName>
                            <rasd:InstanceID>2</rasd:InstanceID>
                            <rasd:ResourceType>4</rasd:ResourceType>
                            <rasd:VirtualQuantity>256</rasd:VirtualQuantity>
                        </Item> */
                    if (uLoop == 1)
                    {
                        strDescription = "Memory Size";
                        type = ovf::ResourceType_Memory; // 4
                        desc.strVBoxCurrent.toInt(uTemp);
                        /* It's alway stored in bytes in VSD according to the old internal agreement within the team */
                        lVirtualQuantity = (int32_t)(uTemp / _1M);
                        strAllocationUnits = "MegaBytes";
                        strCaption = Utf8StrFmt("%d MB of memory", lVirtualQuantity);     // without this ovftool
                                                                                          // won't eat the item
                    }
                    break;

                case VirtualSystemDescriptionType_NVRAM:
                    /*  <Item>
                            <rasd:Caption>NVRAM</rasd:Caption>
                            <rasd:Description>NVRAM pathname</rasd:Description>
                            <rasd:ElementName>NVRAM</rasd:ElementName>
                            <rasd:HostResource>ovf:/file/file1</rasd:HostResource>
                            <rasd:InstanceID>2</rasd:InstanceID>
                            <rasd:ResourceType>0x8000</rasd:ResourceType>
                        </Item> */
                    if (uLoop == 1)
                    {
                        strCaption = "NVRAM";
                        strDescription = "NVRAM pathname";
                        strHostResource = Utf8StrFmt("ovf:/file/file%RI32", ulFile);
                        type = ovf::ResourceType_NVRAM; // 0x8000
                        stack.mapFilesSequence.push_back(strHostResource);

                        /* Store the key details of the NVRAM file for reference when adding
                         * the <File> entry to the <References> element and also when writing
                         * the archive later. */
                        Utf8StrFmt strNvramKey("file%RI32", ulFile++);
                        Utf8Str strNvramSrcPath = desc.strVBoxCurrent;
                        Utf8Str strNvramTargetFile = desc.strOvf;
                        stack.mapNvramFileForOneVM[strNvramKey] =
                            std::pair<Utf8Str, Utf8Str>(strNvramSrcPath, strNvramTargetFile);
                        stack.mapNvramFiles[strNvramKey] =
                            std::pair<Utf8Str, Utf8Str>(strNvramSrcPath, strNvramTargetFile);
                    }
                    break;

                case VirtualSystemDescriptionType_HardDiskControllerIDE:
                    /* <Item>
                            <rasd:Caption>ideController1</rasd:Caption>
                            <rasd:Description>IDE Controller</rasd:Description>
                            <rasd:InstanceId>5</rasd:InstanceId>
                            <rasd:ResourceType>5</rasd:ResourceType>
                            <rasd:Address>1</rasd:Address>
                            <rasd:BusNumber>1</rasd:BusNumber>
                        </Item> */
                    if (uLoop == 1)
                    {
                        strDescription = "IDE Controller";
                        type = ovf::ResourceType_IDEController; // 5
                        strResourceSubType = desc.strVBoxCurrent;

                        if (!lIDEPrimaryControllerIndex)
                        {
                            // first IDE controller:
                            strCaption = "ideController0";
                            lAddress = 0;
                            lBusNumber = 0;
                            // remember this ID
                            idIDEPrimaryController = ulInstanceID;
                            lIDEPrimaryControllerIndex = lIndexThis;
                        }
                        else
                        {
                            // second IDE controller:
                            strCaption = "ideController1";
                            lAddress = 1;
                            lBusNumber = 1;
                            // remember this ID
                            idIDESecondaryController = ulInstanceID;
                            lIDESecondaryControllerIndex = lIndexThis;
                        }
                    }
                    break;

                case VirtualSystemDescriptionType_HardDiskControllerSATA:
                    /*  <Item>
                            <rasd:Caption>sataController0</rasd:Caption>
                            <rasd:Description>SATA Controller</rasd:Description>
                            <rasd:InstanceId>4</rasd:InstanceId>
                            <rasd:ResourceType>20</rasd:ResourceType>
                            <rasd:ResourceSubType>ahci</rasd:ResourceSubType>
                            <rasd:Address>0</rasd:Address>
                            <rasd:BusNumber>0</rasd:BusNumber>
                        </Item>
                    */
                    if (uLoop == 1)
                    {
                        strDescription = "SATA Controller";
                        strCaption = "sataController0";
                        type = ovf::ResourceType_OtherStorageDevice; // 20
                        // it seems that OVFTool always writes these two, and since we can only
                        // have one SATA controller, we'll use this as well
                        lAddress = 0;
                        lBusNumber = 0;

                        if (    desc.strVBoxCurrent.isEmpty()      // AHCI is the default in VirtualBox
                             || (!desc.strVBoxCurrent.compare("ahci", Utf8Str::CaseInsensitive))
                           )
                            strResourceSubType = "AHCI";
                        else
                            throw setError(VBOX_E_NOT_SUPPORTED,
                                            tr("Invalid config string \"%s\" in SATA controller"), desc.strVBoxCurrent.c_str());

                        // remember this ID
                        idSATAController = ulInstanceID;
                        lSATAControllerIndex = lIndexThis;
                    }
                    break;

                case VirtualSystemDescriptionType_HardDiskControllerSCSI:
                case VirtualSystemDescriptionType_HardDiskControllerSAS:
                    /*  <Item>
                            <rasd:Caption>scsiController0</rasd:Caption>
                            <rasd:Description>SCSI Controller</rasd:Description>
                            <rasd:InstanceId>4</rasd:InstanceId>
                            <rasd:ResourceType>6</rasd:ResourceType>
                            <rasd:ResourceSubType>buslogic</rasd:ResourceSubType>
                            <rasd:Address>0</rasd:Address>
                            <rasd:BusNumber>0</rasd:BusNumber>
                        </Item>
                    */
                    if (uLoop == 1)
                    {
                        strDescription = "SCSI Controller";
                        strCaption = "scsiController0";
                        type = ovf::ResourceType_ParallelSCSIHBA; // 6
                        // it seems that OVFTool always writes these two, and since we can only
                        // have one SATA controller, we'll use this as well
                        lAddress = 0;
                        lBusNumber = 0;

                        if (    desc.strVBoxCurrent.isEmpty()      // LsiLogic is the default in VirtualBox
                             || (!desc.strVBoxCurrent.compare("lsilogic", Utf8Str::CaseInsensitive))
                            )
                            strResourceSubType = "lsilogic";
                        else if (!desc.strVBoxCurrent.compare("buslogic", Utf8Str::CaseInsensitive))
                            strResourceSubType = "buslogic";
                        else if (!desc.strVBoxCurrent.compare("lsilogicsas", Utf8Str::CaseInsensitive))
                            strResourceSubType = "lsilogicsas";
                        else
                            throw setError(VBOX_E_NOT_SUPPORTED,
                                           tr("Invalid config string \"%s\" in SCSI/SAS controller"),
                                           desc.strVBoxCurrent.c_str());

                        // remember this ID
                        idSCSIController = ulInstanceID;
                        lSCSIControllerIndex = lIndexThis;
                    }
                    break;


                case VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI:
                    /*  <Item>
                            <rasd:Caption>VirtioSCSIController0</rasd:Caption>
                            <rasd:Description>VirtioSCSI Controller</rasd:Description>
                            <rasd:InstanceId>4</rasd:InstanceId>
                            <rasd:ResourceType>20</rasd:ResourceType>
                            <rasd:Address>0</rasd:Address>
                            <rasd:BusNumber>0</rasd:BusNumber>
                        </Item>
                    */
                    if (uLoop == 1)
                    {
                        strDescription = "VirtioSCSI Controller";
                        strCaption = "virtioSCSIController0";
                        type = ovf::ResourceType_OtherStorageDevice; // 20
                        lAddress = 0;
                        lBusNumber = 0;
                        strResourceSubType = "VirtioSCSI";
                        // remember this ID
                        idVirtioSCSIController = ulInstanceID;
                        lVirtioSCSIControllerIndex = lIndexThis;
                    }
                    break;

                case VirtualSystemDescriptionType_HardDiskControllerNVMe:
                    /*  <Item>
                            <rasd:Caption>NVMeController0</rasd:Caption>
                            <rasd:Description>NVMe Controller</rasd:Description>
                            <rasd:InstanceId>4</rasd:InstanceId>
                            <rasd:ResourceType>20</rasd:ResourceType>
                            <rasd:Address>0</rasd:Address>
                            <rasd:BusNumber>0</rasd:BusNumber>
                        </Item>
                    */
                    if (uLoop == 1)
                    {
                        strDescription = "NVMe Controller";
                        strCaption = "nvmeController0";
                        type = ovf::ResourceType_OtherStorageDevice; // 20
                        lAddress = 0;
                        lBusNumber = 0;
                        strResourceSubType = "NVMe";
                        // remember this ID
                        idNVMeController = ulInstanceID;
                        lNVMeControllerIndex = lIndexThis;
                    }
                    break;

                case VirtualSystemDescriptionType_HardDiskImage:
                    /*  <Item>
                            <rasd:Caption>disk1</rasd:Caption>
                            <rasd:InstanceId>8</rasd:InstanceId>
                            <rasd:ResourceType>17</rasd:ResourceType>
                            <rasd:HostResource>/disk/vmdisk1</rasd:HostResource>
                            <rasd:Parent>4</rasd:Parent>
                            <rasd:AddressOnParent>0</rasd:AddressOnParent>
                        </Item> */
                    if (uLoop == 2)
                    {
                        uint32_t cDisks = (uint32_t)stack.mapDisks.size();
                        Utf8Str strDiskID = Utf8StrFmt("vmdisk%RI32", ++cDisks);

                        strDescription = "Disk Image";
                        strCaption = Utf8StrFmt("disk%RI32", cDisks);        // this is not used for anything else
                        type = ovf::ResourceType_HardDisk; // 17

                        // the following references the "<Disks>" XML block
                        strHostResource = Utf8StrFmt("ovf:/disk/%s", strDiskID.c_str());

                        // controller=<index>;channel=<c>
                        size_t pos1 = desc.strExtraConfigCurrent.find("controller=");
                        size_t pos2 = desc.strExtraConfigCurrent.find("channel=");
                        int32_t lControllerIndex = -1;
                        if (pos1 != Utf8Str::npos)
                        {
                            RTStrToInt32Ex(desc.strExtraConfigCurrent.c_str() + pos1 + 11, NULL, 0, &lControllerIndex);
                            if (lControllerIndex == lIDEPrimaryControllerIndex)
                                ulParent = idIDEPrimaryController;
                            else if (lControllerIndex == lIDESecondaryControllerIndex)
                                ulParent = idIDESecondaryController;
                            else if (lControllerIndex == lSCSIControllerIndex)
                                ulParent = idSCSIController;
                            else if (lControllerIndex == lSATAControllerIndex)
                                ulParent = idSATAController;
                            else if (lControllerIndex == lVirtioSCSIControllerIndex)
                                ulParent = idVirtioSCSIController;
                            else if (lControllerIndex == lNVMeControllerIndex)
                                ulParent = idNVMeController;
                        }
                        if (pos2 != Utf8Str::npos)
                            RTStrToInt32Ex(desc.strExtraConfigCurrent.c_str() + pos2 + 8, NULL, 0, &lAddressOnParent);

                        LogFlowFunc(("HardDiskImage details: pos1=%d, pos2=%d, lControllerIndex=%d, lIDEPrimaryControllerIndex=%d, lIDESecondaryControllerIndex=%d, ulParent=%d, lAddressOnParent=%d\n",
                                     pos1, pos2, lControllerIndex, lIDEPrimaryControllerIndex, lIDESecondaryControllerIndex,
                                     ulParent, lAddressOnParent));

                        if (    !ulParent
                             || lAddressOnParent == -1
                           )
                            throw setError(VBOX_E_NOT_SUPPORTED,
                                           tr("Missing or bad extra config string in hard disk image: \"%s\""),
                                           desc.strExtraConfigCurrent.c_str());
                        stack.mapDisks[strDiskID] = &desc;

                        // Use the list stack.mapFilesSequence to store where the disks (and any
                        // other collected files) according to each <VirtualSystem> in the OVF
                        // description file.
                        stack.mapFilesSequence.push_back(strHostResource);
                        stack.mapDiskSequenceForOneVM.push_back(strDiskID);
                    }
                    break;

                case VirtualSystemDescriptionType_Floppy:
                    if (uLoop == 1)
                    {
                        strDescription = "Floppy Drive";
                        strCaption = "floppy0";         // this is what OVFTool writes
                        type = ovf::ResourceType_FloppyDrive; // 14
                        lAutomaticAllocation = 0;
                        lAddressOnParent = 0;           // this is what OVFTool writes
                    }
                    break;

                case VirtualSystemDescriptionType_CDROM:
                    /*  <Item>
                            <rasd:Caption>cdrom1</rasd:Caption>
                            <rasd:InstanceId>8</rasd:InstanceId>
                            <rasd:ResourceType>15</rasd:ResourceType>
                            <rasd:HostResource>/disk/cdrom1</rasd:HostResource>
                            <rasd:Parent>4</rasd:Parent>
                            <rasd:AddressOnParent>0</rasd:AddressOnParent>
                        </Item> */
                    if (uLoop == 2)
                    {
                        uint32_t cDisks = (uint32_t)stack.mapDisks.size();
                        Utf8Str strDiskID = Utf8StrFmt("iso%RI32", ++cDisks);
                        ++cDVDs;
                        strDescription = "CD-ROM Drive";
                        strCaption = Utf8StrFmt("cdrom%RI32", cDVDs);     // OVFTool starts with 1
                        type = ovf::ResourceType_CDDrive; // 15
                        lAutomaticAllocation = 1;

                        //skip empty Medium. There are no information to add into section <References> or <DiskSection>
                        if (desc.strVBoxCurrent.isNotEmpty() &&
                            desc.skipIt == false)
                        {
                            // the following references the "<Disks>" XML block
                            strHostResource = Utf8StrFmt("ovf:/disk/%s", strDiskID.c_str());
                            stack.mapFilesSequence.push_back(strHostResource);
                        }

                        // controller=<index>;channel=<c>
                        size_t pos1 = desc.strExtraConfigCurrent.find("controller=");
                        size_t pos2 = desc.strExtraConfigCurrent.find("channel=");
                        int32_t lControllerIndex = -1;
                        if (pos1 != Utf8Str::npos)
                        {
                            RTStrToInt32Ex(desc.strExtraConfigCurrent.c_str() + pos1 + 11, NULL, 0, &lControllerIndex);
                            if (lControllerIndex == lIDEPrimaryControllerIndex)
                                ulParent = idIDEPrimaryController;
                            else if (lControllerIndex == lIDESecondaryControllerIndex)
                                ulParent = idIDESecondaryController;
                            else if (lControllerIndex == lSCSIControllerIndex)
                                ulParent = idSCSIController;
                            else if (lControllerIndex == lSATAControllerIndex)
                                ulParent = idSATAController;
                            else if (lControllerIndex == lVirtioSCSIControllerIndex)
                                ulParent = idVirtioSCSIController;
                        }
                        if (pos2 != Utf8Str::npos)
                            RTStrToInt32Ex(desc.strExtraConfigCurrent.c_str() + pos2 + 8, NULL, 0, &lAddressOnParent);

                        LogFlowFunc(("DVD drive details: pos1=%d, pos2=%d, lControllerIndex=%d, lIDEPrimaryControllerIndex=%d, lIDESecondaryControllerIndex=%d, ulParent=%d, lAddressOnParent=%d\n",
                                     pos1, pos2, lControllerIndex, lIDEPrimaryControllerIndex,
                                     lIDESecondaryControllerIndex, ulParent, lAddressOnParent));

                        if (    !ulParent
                             || lAddressOnParent == -1
                           )
                            throw setError(VBOX_E_NOT_SUPPORTED,
                                           tr("Missing or bad extra config string in DVD drive medium: \"%s\""),
                                           desc.strExtraConfigCurrent.c_str());

                        stack.mapDisks[strDiskID] = &desc;

                        //use the list stack.mapDiskSequence where the disks go as the "VirtualSystem" should be placed
                        //in the OVF description file.
                        stack.mapDiskSequenceForOneVM.push_back(strDiskID);
                        // there is no DVD drive map to update because it is
                        // handled completely with this entry.
                    }
                    break;

                case VirtualSystemDescriptionType_NetworkAdapter:
                    /* <Item>
                            <rasd:AutomaticAllocation>true</rasd:AutomaticAllocation>
                            <rasd:Caption>Ethernet adapter on 'VM Network'</rasd:Caption>
                            <rasd:Connection>VM Network</rasd:Connection>
                            <rasd:ElementName>VM network</rasd:ElementName>
                            <rasd:InstanceID>3</rasd:InstanceID>
                            <rasd:ResourceType>10</rasd:ResourceType>
                        </Item> */
                    if (uLoop == 2)
                    {
                        lAutomaticAllocation = 1;
                        strCaption = Utf8StrFmt("Ethernet adapter on '%s'", desc.strOvf.c_str());
                        type = ovf::ResourceType_EthernetAdapter; // 10
                        /* Set the hardware type to something useful.
                            * To be compatible with vmware & others we set
                            * PCNet32 for our PCNet types & E1000 for the
                            * E1000 cards. */
                        switch (desc.strVBoxCurrent.toInt32())
                        {
                            case NetworkAdapterType_Am79C970A:
                            case NetworkAdapterType_Am79C973: strResourceSubType = "PCNet32"; break;
#ifdef VBOX_WITH_E1000
                            case NetworkAdapterType_I82540EM:
                            case NetworkAdapterType_I82545EM:
                            case NetworkAdapterType_I82543GC: strResourceSubType = "E1000"; break;
#endif /* VBOX_WITH_E1000 */
                        }
                        strConnection = desc.strOvf;

                        stack.mapNetworks[desc.strOvf] = true;
                    }
                    break;

                case VirtualSystemDescriptionType_USBController:
                    /*  <Item ovf:required="false">
                            <rasd:Caption>usb</rasd:Caption>
                            <rasd:Description>USB Controller</rasd:Description>
                            <rasd:InstanceId>3</rasd:InstanceId>
                            <rasd:ResourceType>23</rasd:ResourceType>
                            <rasd:Address>0</rasd:Address>
                            <rasd:BusNumber>0</rasd:BusNumber>
                        </Item> */
                    if (uLoop == 1)
                    {
                        strDescription = "USB Controller";
                        strCaption = "usb";
                        type = ovf::ResourceType_USBController; // 23
                        lAddress = 0;                   // this is what OVFTool writes
                        lBusNumber = 0;                 // this is what OVFTool writes
                    }
                    break;

                case VirtualSystemDescriptionType_SoundCard:
                /*  <Item ovf:required="false">
                        <rasd:Caption>sound</rasd:Caption>
                        <rasd:Description>Sound Card</rasd:Description>
                        <rasd:InstanceId>10</rasd:InstanceId>
                        <rasd:ResourceType>35</rasd:ResourceType>
                        <rasd:ResourceSubType>ensoniq1371</rasd:ResourceSubType>
                        <rasd:AutomaticAllocation>false</rasd:AutomaticAllocation>
                        <rasd:AddressOnParent>3</rasd:AddressOnParent>
                    </Item> */
                    if (uLoop == 1)
                    {
                        strDescription = "Sound Card";
                        strCaption = "sound";
                        type = ovf::ResourceType_SoundCard; // 35
                        strResourceSubType = desc.strOvf;       // e.g. ensoniq1371
                        lAutomaticAllocation = 0;
                        lAddressOnParent = 3;               // what gives? this is what OVFTool writes
                    }
                    break;

                default: break; /* Shut up MSC. */
            }

            if (type)
            {
                xml::ElementNode *pItem;
                xml::ElementNode *pItemHelper;
                RTCString itemElement;
                RTCString itemElementHelper;

                if (enFormat == ovf::OVFVersion_2_0)
                {
                    if(uLoop == 2)
                    {
                        if (desc.type == VirtualSystemDescriptionType_NetworkAdapter)
                        {
                            itemElement = "epasd:";
                            pItem = pelmVirtualHardwareSection->createChild("EthernetPortItem");
                        }
                        else if (desc.type == VirtualSystemDescriptionType_CDROM ||
                                 desc.type == VirtualSystemDescriptionType_HardDiskImage)
                        {
                            itemElement = "sasd:";
                            pItem = pelmVirtualHardwareSection->createChild("StorageItem");
                        }
                        else
                            pItem = NULL;
                    }
                    else
                    {
                        itemElement = "rasd:";
                        pItem = pelmVirtualHardwareSection->createChild("Item");
                    }
                }
                else
                {
                    itemElement = "rasd:";
                    pItem = pelmVirtualHardwareSection->createChild("Item");
                }

                // NOTE: DO NOT CHANGE THE ORDER of these items! The OVF standards prescribes that
                // the elements from the rasd: namespace must be sorted by letter, and VMware
                // actually requires this as well (see public bug #6612)

                if (lAddress != -1)
                {
                    //pItem->createChild("rasd:Address")->addContent(Utf8StrFmt("%d", lAddress));
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("Address").c_str());
                    pItemHelper->addContent(Utf8StrFmt("%d", lAddress));
                }

                if (lAddressOnParent != -1)
                {
                    //pItem->createChild("rasd:AddressOnParent")->addContent(Utf8StrFmt("%d", lAddressOnParent));
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("AddressOnParent").c_str());
                    pItemHelper->addContent(Utf8StrFmt("%d", lAddressOnParent));
                }

                if (!strAllocationUnits.isEmpty())
                {
                    //pItem->createChild("rasd:AllocationUnits")->addContent(strAllocationUnits);
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("AllocationUnits").c_str());
                    pItemHelper->addContent(strAllocationUnits);
                }

                if (lAutomaticAllocation != -1)
                {
                    //pItem->createChild("rasd:AutomaticAllocation")->addContent( (lAutomaticAllocation) ? "true" : "false" );
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("AutomaticAllocation").c_str());
                    pItemHelper->addContent((lAutomaticAllocation) ? "true" : "false" );
                }

                if (lBusNumber != -1)
                {
                    if (enFormat == ovf::OVFVersion_0_9)
                    {
                        // BusNumber is invalid OVF 1.0 so only write it in 0.9 mode for OVFTool
                        //pItem->createChild("rasd:BusNumber")->addContent(Utf8StrFmt("%d", lBusNumber));
                        itemElementHelper = itemElement;
                        pItemHelper = pItem->createChild(itemElementHelper.append("BusNumber").c_str());
                        pItemHelper->addContent(Utf8StrFmt("%d", lBusNumber));
                    }
                }

                if (!strCaption.isEmpty())
                {
                    //pItem->createChild("rasd:Caption")->addContent(strCaption);
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("Caption").c_str());
                    pItemHelper->addContent(strCaption);
                }

                if (!strConnection.isEmpty())
                {
                    //pItem->createChild("rasd:Connection")->addContent(strConnection);
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("Connection").c_str());
                    pItemHelper->addContent(strConnection);
                }

                if (!strDescription.isEmpty())
                {
                    //pItem->createChild("rasd:Description")->addContent(strDescription);
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("Description").c_str());
                    pItemHelper->addContent(strDescription);
                }

                if (!strCaption.isEmpty())
                {
                        if (enFormat == ovf::OVFVersion_1_0)
                        {
                            //pItem->createChild("rasd:ElementName")->addContent(strCaption);
                            itemElementHelper = itemElement;
                            pItemHelper = pItem->createChild(itemElementHelper.append("ElementName").c_str());
                            pItemHelper->addContent(strCaption);
                        }
                }

                if (!strHostResource.isEmpty())
                {
                    //pItem->createChild("rasd:HostResource")->addContent(strHostResource);
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("HostResource").c_str());
                    pItemHelper->addContent(strHostResource);
                }

                {
                    // <rasd:InstanceID>1</rasd:InstanceID>
                    itemElementHelper = itemElement;
                    if (enFormat == ovf::OVFVersion_0_9)
                        //pelmInstanceID = pItem->createChild("rasd:InstanceId");
                        pItemHelper = pItem->createChild(itemElementHelper.append("InstanceId").c_str());
                    else
                        //pelmInstanceID = pItem->createChild("rasd:InstanceID");      // capitalization changed...
                        pItemHelper = pItem->createChild(itemElementHelper.append("InstanceID").c_str());

                    pItemHelper->addContent(Utf8StrFmt("%d", ulInstanceID++));
                }

                if (ulParent)
                {
                    //pItem->createChild("rasd:Parent")->addContent(Utf8StrFmt("%d", ulParent));
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("Parent").c_str());
                    pItemHelper->addContent(Utf8StrFmt("%d", ulParent));
                }

                if (!strResourceSubType.isEmpty())
                {
                    //pItem->createChild("rasd:ResourceSubType")->addContent(strResourceSubType);
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("ResourceSubType").c_str());
                    pItemHelper->addContent(strResourceSubType);
                }

                {
                    // <rasd:ResourceType>3</rasd:ResourceType>
                    //pItem->createChild("rasd:ResourceType")->addContent(Utf8StrFmt("%d", type));
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("ResourceType").c_str());
                    pItemHelper->addContent(Utf8StrFmt("%d", type));
                }

                // <rasd:VirtualQuantity>1</rasd:VirtualQuantity>
                if (lVirtualQuantity != -1)
                {
                    //pItem->createChild("rasd:VirtualQuantity")->addContent(Utf8StrFmt("%d", lVirtualQuantity));
                    itemElementHelper = itemElement;
                    pItemHelper = pItem->createChild(itemElementHelper.append("VirtualQuantity").c_str());
                    pItemHelper->addContent(Utf8StrFmt("%d", lVirtualQuantity));
                }
            }
        }
    } // for (size_t uLoop = 1; uLoop <= 2; ++uLoop)

    // now that we're done with the official OVF <Item> tags under <VirtualSystem>, write out VirtualBox XML
    // under the vbox: namespace
    xml::ElementNode *pelmVBoxMachine = pelmVirtualSystem->createChild("vbox:Machine");
    // ovf:required="false" tells other OVF parsers that they can ignore this thing
    pelmVBoxMachine->setAttribute("ovf:required", "false");
    // ovf:Info element is required or VMware will bail out on the vbox:Machine element
    pelmVBoxMachine->createChild("ovf:Info")->addContent("Complete VirtualBox machine configuration in VirtualBox format");

    // create an empty machine config
    // use the same settings version as the current VM settings file
    settings::MachineConfigFile *pConfig = new settings::MachineConfigFile(&vsdescThis->m->pMachine->i_getSettingsFileFull());

    writeLock.release();
    try
    {
        AutoWriteLock machineLock(vsdescThis->m->pMachine COMMA_LOCKVAL_SRC_POS);
        // fill the machine config
        vsdescThis->m->pMachine->i_copyMachineDataToSettings(*pConfig);
        pConfig->machineUserData.strName = strVMName;

        // Apply export tweaks to machine settings
        bool fStripAllMACs = m->optListExport.contains(ExportOptions_StripAllMACs);
        bool fStripAllNonNATMACs = m->optListExport.contains(ExportOptions_StripAllNonNATMACs);
        if (fStripAllMACs || fStripAllNonNATMACs)
        {
            for (settings::NetworkAdaptersList::iterator
                 it = pConfig->hardwareMachine.llNetworkAdapters.begin();
                 it != pConfig->hardwareMachine.llNetworkAdapters.end();
                 ++it)
            {
                settings::NetworkAdapter &nic = *it;
                if (fStripAllMACs || (fStripAllNonNATMACs && nic.mode != NetworkAttachmentType_NAT))
                    nic.strMACAddress.setNull();
            }
        }

        // write the machine config to the vbox:Machine element
        pConfig->buildMachineXML(*pelmVBoxMachine,
                                   settings::MachineConfigFile::BuildMachineXML_WriteVBoxVersionAttribute
                                 /*| settings::MachineConfigFile::BuildMachineXML_SkipRemovableMedia*/
                                 | settings::MachineConfigFile::BuildMachineXML_SuppressSavedState,
                                        // but not BuildMachineXML_IncludeSnapshots nor BuildMachineXML_MediaRegistry
                                 pllElementsWithUuidAttributes);
        delete pConfig;
    }
    catch (...)
    {
        writeLock.acquire();
        delete pConfig;
        throw;
    }
    writeLock.acquire();
}

/**
 * Actual worker code for writing out OVF/OVA to disk. This is called from Appliance::taskThreadWriteOVF()
 * and therefore runs on the OVF/OVA write worker thread.
 *
 * This runs in one context:
 *
 * 1) in a first worker thread; in that case, Appliance::Write() called Appliance::i_writeImpl();
 *
 * @param pTask
 * @return
 */
HRESULT Appliance::i_writeFS(TaskOVF *pTask)
{
    LogFlowFuncEnter();
    LogFlowFunc(("ENTER appliance %p\n", this));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    HRESULT hrc = S_OK;

    // Lock the media tree early to make sure nobody else tries to make changes
    // to the tree. Also lock the IAppliance object for writing.
    AutoMultiWriteLock2 multiLock(&mVirtualBox->i_getMediaTreeLockHandle(), this->lockHandle() COMMA_LOCKVAL_SRC_POS);
    // Additional protect the IAppliance object, cause we leave the lock
    // when starting the disk export and we don't won't block other
    // callers on this lengthy operations.
    m->state = ApplianceExporting;

    if (pTask->locInfo.strPath.endsWith(".ovf", Utf8Str::CaseInsensitive))
        hrc = i_writeFSOVF(pTask, multiLock);
    else
        hrc = i_writeFSOVA(pTask, multiLock);

    // reset the state so others can call methods again
    m->state = ApplianceIdle;

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    LogFlowFuncLeave();
    return hrc;
}

HRESULT Appliance::i_writeFSOVF(TaskOVF *pTask, AutoWriteLockBase& writeLock)
{
    LogFlowFuncEnter();

    /*
     * Create write-to-dir file system stream for the target directory.
     * This unifies the disk access with the TAR based OVA variant.
     */
    HRESULT         hrc;
    int             vrc;
    RTVFSFSSTREAM   hVfsFss2Dir = NIL_RTVFSFSSTREAM;
    try
    {
        Utf8Str strTargetDir(pTask->locInfo.strPath);
        strTargetDir.stripFilename();
        vrc = RTVfsFsStrmToNormalDir(strTargetDir.c_str(), 0 /*fFlags*/, &hVfsFss2Dir);
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = setErrorVrc(vrc, tr("Failed to open directory '%s' (%Rrc)"), strTargetDir.c_str(), vrc);
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hrc))
    {
        /*
         * Join i_writeFSOVA.  On failure, delete (undo) anything we might
         * have written to the disk before failing.
         */
        hrc = i_writeFSImpl(pTask, writeLock, hVfsFss2Dir);
        if (FAILED(hrc))
            RTVfsFsStrmToDirUndo(hVfsFss2Dir);
        RTVfsFsStrmRelease(hVfsFss2Dir);
    }

    LogFlowFuncLeave();
    return hrc;
}

HRESULT Appliance::i_writeFSOVA(TaskOVF *pTask, AutoWriteLockBase &writeLock)
{
    LogFlowFuncEnter();

    /*
     * Open the output file and attach a TAR creator to it.
     * The OVF 1.1.0 spec specifies the TAR format to be compatible with USTAR
     * according to POSIX 1003.1-2008.  We use the 1988 spec here as it's the
     * only variant we currently implement.
     */
    HRESULT hrc;
    RTVFSIOSTREAM hVfsIosTar;
    int vrc = RTVfsIoStrmOpenNormal(pTask->locInfo.strPath.c_str(),
                                    RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE,
                                    &hVfsIosTar);
    if (RT_SUCCESS(vrc))
    {
        RTVFSFSSTREAM hVfsFssTar;
        vrc = RTZipTarFsStreamToIoStream(hVfsIosTar, RTZIPTARFORMAT_USTAR, 0 /*fFlags*/, &hVfsFssTar);
        RTVfsIoStrmRelease(hVfsIosTar);
        if (RT_SUCCESS(vrc))
        {
            RTZipTarFsStreamSetFileMode(hVfsFssTar, 0660, 0440);
            RTZipTarFsStreamSetOwner(hVfsFssTar, VBOX_VERSION_MAJOR,
                                       pTask->enFormat == ovf::OVFVersion_0_9 ? "vboxovf09"
                                     : pTask->enFormat == ovf::OVFVersion_1_0 ? "vboxovf10"
                                     : pTask->enFormat == ovf::OVFVersion_2_0 ? "vboxovf20"
                                     :                                          "vboxovf");
            RTZipTarFsStreamSetGroup(hVfsFssTar, VBOX_VERSION_MINOR,
                                     Utf8StrFmt("vbox_v" RT_XSTR(VBOX_VERSION_MAJOR) "." RT_XSTR(VBOX_VERSION_MINOR) "."
                                                RT_XSTR(VBOX_VERSION_BUILD) "r%RU32", RTBldCfgRevision()).c_str());

            hrc = i_writeFSImpl(pTask, writeLock, hVfsFssTar);
            RTVfsFsStrmRelease(hVfsFssTar);
        }
        else
            hrc = setErrorVrc(vrc, tr("Failed create TAR creator for '%s' (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

        /* Delete the OVA on failure. */
        if (FAILED(hrc))
            RTFileDelete(pTask->locInfo.strPath.c_str());
    }
    else
        hrc = setErrorVrc(vrc, tr("Failed to open '%s' for writing (%Rrc)"), pTask->locInfo.strPath.c_str(), vrc);

    LogFlowFuncLeave();
    return hrc;
}

/**
 * Upload the image to the OCI Storage service, next import the
 * uploaded image into internal OCI image format and launch an
 * instance with this image in the OCI Compute service.
 */
HRESULT Appliance::i_exportCloudImpl(TaskCloud *pTask)
{
    LogFlowFuncEnter();

    HRESULT hrc = S_OK;
    ComPtr<ICloudProviderManager> cpm;
    hrc = mVirtualBox->COMGETTER(CloudProviderManager)(cpm.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud provider manager object wasn't found"), __FUNCTION__);

    Utf8Str strProviderName = pTask->locInfo.strProvider;
    ComPtr<ICloudProvider> cloudProvider;
    ComPtr<ICloudProfile> cloudProfile;
    hrc = cpm->GetProviderByShortName(Bstr(strProviderName.c_str()).raw(), cloudProvider.asOutParam());

    if (FAILED(hrc))
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud provider object wasn't found"), __FUNCTION__);

    ComPtr<IVirtualSystemDescription> vsd = m->virtualSystemDescriptions.front();

    com::SafeArray<VirtualSystemDescriptionType_T> retTypes;
    com::SafeArray<BSTR> aRefs;
    com::SafeArray<BSTR> aOvfValues;
    com::SafeArray<BSTR> aVBoxValues;
    com::SafeArray<BSTR> aExtraConfigValues;

    hrc = vsd->GetDescriptionByType(VirtualSystemDescriptionType_CloudProfileName,
                                    ComSafeArrayAsOutParam(retTypes),
                                    ComSafeArrayAsOutParam(aRefs),
                                    ComSafeArrayAsOutParam(aOvfValues),
                                    ComSafeArrayAsOutParam(aVBoxValues),
                                    ComSafeArrayAsOutParam(aExtraConfigValues));
    if (FAILED(hrc))
        return hrc;

    Utf8Str profileName(aVBoxValues[0]);
    if (profileName.isEmpty())
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud user profile name wasn't found"), __FUNCTION__);

    hrc = cloudProvider->GetProfileByName(aVBoxValues[0], cloudProfile.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud profile object wasn't found"), __FUNCTION__);

    ComObjPtr<ICloudClient> cloudClient;
    hrc = cloudProfile->CreateCloudClient(cloudClient.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("%s: Cloud client object wasn't found"), __FUNCTION__);

    if (m->virtualSystemDescriptions.size() == 1)
    {
        ComPtr<IVirtualBox> VBox(mVirtualBox);
        hrc = cloudClient->ExportVM(m->virtualSystemDescriptions.front(), pTask->pProgress);
    }
    else
        hrc = setErrorVrc(VERR_MISMATCH, tr("Export to Cloud isn't supported for more than one VM instance."));

    LogFlowFuncLeave();
    return hrc;
}


/**
 * Writes the Oracle Public Cloud appliance.
 *
 * It expect raw disk images inside a gzipped tarball.  We enable sparse files
 * to save diskspace on the target host system.
 */
HRESULT Appliance::i_writeFSOPC(TaskOPC *pTask)
{
    LogFlowFuncEnter();
    HRESULT hrc = S_OK;

    // Lock the media tree early to make sure nobody else tries to make changes
    // to the tree. Also lock the IAppliance object for writing.
    AutoMultiWriteLock2 multiLock(&mVirtualBox->i_getMediaTreeLockHandle(), this->lockHandle() COMMA_LOCKVAL_SRC_POS);
    // Additional protect the IAppliance object, cause we leave the lock
    // when starting the disk export and we don't won't block other
    // callers on this lengthy operations.
    m->state = ApplianceExporting;

    /*
     * We're duplicating parts of i_writeFSImpl here because that's simpler
     * and creates less spaghetti code.
     */
    std::list<Utf8Str> lstTarballs;

    /*
     * Use i_buildXML to build a stack of disk images.  We don't care about the XML doc here.
     */
    XMLStack stack;
    {
        xml::Document doc;
        i_buildXML(multiLock, doc, stack, ovf::OVFVersion_2_0);
    }

    /*
     * Process the disk images.
     */
    unsigned cTarballs = 0;
    for (list<Utf8Str>::const_iterator it = stack.mapFilesSequence.begin();
         it != stack.mapFilesSequence.end();
         ++it)
    {
        const Utf8Str                       &strHostResource = *it;

        /* Skip any NVRAM files */
        if (strHostResource.startsWith("ovf:/file/"))
            continue;

        /* Skip over the 'ovf:/disk/' prefix. */
        const Utf8Str                       strDiskID = strHostResource.substr(10);
        const VirtualSystemDescriptionEntry *pDiskEntry = stack.mapDisks[strDiskID];
        const Utf8Str                       &strSrcFilePath = pDiskEntry->strVBoxCurrent;  // where the VBox image is

        /*
         * Some skipping.
         */
        if (pDiskEntry->skipIt)
            continue;

        /* Skip empty media (DVD-ROM, floppy). */
        if (strSrcFilePath.isEmpty())
            continue;

        /* Only deal with harddisk and DVD-ROMs, skip any floppies for now. */
        if (   pDiskEntry->type != VirtualSystemDescriptionType_HardDiskImage
            && pDiskEntry->type != VirtualSystemDescriptionType_CDROM)
            continue;

        /*
         * Locate the Medium object for this entry (by location/path).
         */
        Log(("Finding source disk \"%s\"\n", strSrcFilePath.c_str()));
        ComObjPtr<Medium> ptrSourceDisk;
        if (pDiskEntry->type == VirtualSystemDescriptionType_HardDiskImage)
            hrc = mVirtualBox->i_findHardDiskByLocation(strSrcFilePath, true /*aSetError*/, &ptrSourceDisk);
        else
            hrc = mVirtualBox->i_findDVDOrFloppyImage(DeviceType_DVD, NULL /*aId*/, strSrcFilePath,
                                                      true /*aSetError*/, &ptrSourceDisk);
        if (FAILED(hrc))
            break;
        if (strSrcFilePath.isEmpty())
            continue;

        /*
         * Figure out the names.
         */

        /* The name inside the tarball.  Replace the suffix of harddisk images with ".img". */
        Utf8Str strInsideName = pDiskEntry->strOvf;
        if (pDiskEntry->type == VirtualSystemDescriptionType_HardDiskImage)
            strInsideName.stripSuffix().append(".img");

        /* The first tarball we create uses the specified name. Subsequent
           takes the name from the disk entry or something. */
        Utf8Str strTarballPath = pTask->locInfo.strPath;
        if (cTarballs > 0)
        {
            strTarballPath.stripFilename().append(RTPATH_SLASH_STR).append(pDiskEntry->strOvf);
            const char *pszExt = RTPathSuffix(pDiskEntry->strOvf.c_str());
            if (pszExt && pszExt[0] == '.' && pszExt[1] != '\0')
            {
                strTarballPath.stripSuffix();
                if (pDiskEntry->type != VirtualSystemDescriptionType_HardDiskImage)
                    strTarballPath.append("_").append(&pszExt[1]);
            }
            strTarballPath.append(".tar.gz");
        }
        cTarballs++;

        /*
         * Create the tar output stream.
         */
        RTVFSIOSTREAM hVfsIosFile;
        int vrc = RTVfsIoStrmOpenNormal(strTarballPath.c_str(),
                                        RTFILE_O_CREATE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE,
                                        &hVfsIosFile);
        if (RT_SUCCESS(vrc))
        {
            RTVFSIOSTREAM hVfsIosGzip = NIL_RTVFSIOSTREAM;
            vrc = RTZipGzipCompressIoStream(hVfsIosFile, 0 /*fFlags*/, 6 /*uLevel*/, &hVfsIosGzip);
            RTVfsIoStrmRelease(hVfsIosFile);

            /** @todo insert I/O thread here between gzip and the tar creator. Needs
             *        implementing. */

            RTVFSFSSTREAM hVfsFssTar = NIL_RTVFSFSSTREAM;
            if (RT_SUCCESS(vrc))
                vrc = RTZipTarFsStreamToIoStream(hVfsIosGzip, RTZIPTARFORMAT_GNU, RTZIPTAR_C_SPARSE, &hVfsFssTar);
            RTVfsIoStrmRelease(hVfsIosGzip);
            if (RT_SUCCESS(vrc))
            {
                RTZipTarFsStreamSetFileMode(hVfsFssTar, 0660, 0440);
                RTZipTarFsStreamSetOwner(hVfsFssTar, VBOX_VERSION_MAJOR, "vboxopc10");
                RTZipTarFsStreamSetGroup(hVfsFssTar, VBOX_VERSION_MINOR,
                                         Utf8StrFmt("vbox_v" RT_XSTR(VBOX_VERSION_MAJOR) "." RT_XSTR(VBOX_VERSION_MINOR) "."
                                         RT_XSTR(VBOX_VERSION_BUILD) "r%RU32", RTBldCfgRevision()).c_str());

                /*
                 * Let the Medium code do the heavy work.
                 *
                 * The exporting requests a lock on the media tree. So temporarily
                 * leave the appliance lock.
                 */
                multiLock.release();

                pTask->pProgress->SetNextOperation(BstrFmt(tr("Exporting to disk image '%Rbn'"), strTarballPath.c_str()).raw(),
                                                   pDiskEntry->ulSizeMB);     // operation's weight, as set up
                                                                              // with the IProgress originally
                hrc = ptrSourceDisk->i_addRawToFss(strInsideName.c_str(), m->m_pSecretKeyStore, hVfsFssTar,
                                                   pTask->pProgress, true /*fSparse*/);

                multiLock.acquire();
                if (SUCCEEDED(hrc))
                {
                    /*
                     * Complete and close the tarball.
                     */
                    vrc = RTVfsFsStrmEnd(hVfsFssTar);
                    RTVfsFsStrmRelease(hVfsFssTar);
                    hVfsFssTar = NIL_RTVFSFSSTREAM;
                    if (RT_SUCCESS(vrc))
                    {
                        /* Remember the tarball name for cleanup. */
                        try
                        {
                            lstTarballs.push_back(strTarballPath.c_str());
                            strTarballPath.setNull();
                        }
                        catch (std::bad_alloc &)
                        { hrc = E_OUTOFMEMORY; }
                    }
                    else
                        hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                           tr("Error completing TAR file '%s' (%Rrc)"), strTarballPath.c_str(), vrc);
                }
            }
            else
                hrc = setErrorVrc(vrc, tr("Failed to TAR creator instance for '%s' (%Rrc)"), strTarballPath.c_str(), vrc);

            if (FAILED(hrc) && strTarballPath.isNotEmpty())
                RTFileDelete(strTarballPath.c_str());
        }
        else
            hrc = setErrorVrc(vrc, tr("Failed to create '%s' (%Rrc)"), strTarballPath.c_str(), vrc);
        if (FAILED(hrc))
            break;
    }

    /*
     * Delete output files on failure.
     */
    if (FAILED(hrc))
        for (list<Utf8Str>::const_iterator it = lstTarballs.begin(); it != lstTarballs.end(); ++it)
            RTFileDelete(it->c_str());

    // reset the state so others can call methods again
    m->state = ApplianceIdle;

    LogFlowFuncLeave();
    return hrc;

}

HRESULT Appliance::i_writeFSImpl(TaskOVF *pTask, AutoWriteLockBase &writeLock, RTVFSFSSTREAM hVfsFssDst)
{
    LogFlowFuncEnter();

    HRESULT hrc = S_OK;
    int vrc;
    try
    {
        // the XML stack contains two maps for disks and networks, which allows us to
        // a) have a list of unique disk names (to make sure the same disk name is only added once)
        // and b) keep a list of all networks
        XMLStack stack;
        // Scope this to free the memory as soon as this is finished
        {
            /* Construct the OVF name. */
            Utf8Str strOvfFile(pTask->locInfo.strPath);
            strOvfFile.stripPath().stripSuffix().append(".ovf");

            /* Render a valid ovf document into a memory buffer.  The unknown
               version upgrade relates to the OPC hack up in Appliance::write(). */
            xml::Document doc;
            i_buildXML(writeLock, doc, stack,
                       pTask->enFormat != ovf::OVFVersion_unknown ? pTask->enFormat : ovf::OVFVersion_2_0);

            void *pvBuf = NULL;
            size_t cbSize = 0;
            xml::XmlMemWriter writer;
            writer.write(doc, &pvBuf, &cbSize);
            if (RT_UNLIKELY(!pvBuf))
                throw setError(VBOX_E_FILE_ERROR, tr("Could not create OVF file '%s'"), strOvfFile.c_str());

            /* Write the ovf file to "disk". */
            hrc = i_writeBufferToFile(hVfsFssDst, strOvfFile.c_str(), pvBuf, cbSize);
            if (FAILED(hrc))
                throw hrc;
        }

        // We need a proper format description
        ComObjPtr<MediumFormat> formatTemp;

        ComObjPtr<MediumFormat> format;
        // Scope for the AutoReadLock
        {
            SystemProperties *pSysProps = mVirtualBox->i_getSystemProperties();
            AutoReadLock propsLock(pSysProps COMMA_LOCKVAL_SRC_POS);
            // We are always exporting to VMDK stream optimized for now
            formatTemp = pSysProps->i_mediumFormatFromExtension("iso");

            format = pSysProps->i_mediumFormat("VMDK");
            if (format.isNull())
                throw setError(VBOX_E_NOT_SUPPORTED,
                               tr("Invalid medium storage format"));
        }

        // Finally, write out the disks!
        //use the list stack.mapFilesSequence where the disks were put as the "VirtualSystem"s had been placed
        //in the OVF description file. I.e. we have one "VirtualSystem" in the OVF file, we extract all disks
        //attached to it. And these disks are stored in the stack.mapFilesSequence. Next we shift to the next
        //"VirtualSystem" and repeat the operation.
        //And here we go through the list and extract all disks in the same sequence
        for (list<Utf8Str>::const_iterator
             it = stack.mapFilesSequence.begin();
             it != stack.mapFilesSequence.end();
             ++it)
        {
            const Utf8Str &strHostResource = *it;

            /* We process the NVRAM files here along with the disks to maintain the
             * order which the files and disks were added to the 'mapFilesSequence' list
             * which is the same sequence used to write out the <File> elements in the
             * <References> section. This must also be the same order that the files are
             * added to the archive as required by the OVF specification in section 5.3
             * which is why we add them here in the midst of the disk processing. */
            if (strHostResource.startsWith("ovf:/file/"))
            {
                const Utf8Str strNvramKey = strHostResource.substr(10);
                std::map<Utf8Str, std::pair<Utf8Str, Utf8Str> >::const_iterator
                    itMap = stack.mapNvramFiles.find(strNvramKey);
                if (itMap == stack.mapNvramFiles.end())
                    throw setError(E_FAIL, tr("Failed to find NVRAM file '%s' in map", strNvramKey.c_str()));

                Utf8Str strNvramSrcPath = itMap->second.first;
                Utf8Str strNvramTargetFile = itMap->second.second;
                hrc = i_writeNvramFileToIoStream(strNvramSrcPath, strNvramTargetFile, hVfsFssDst);
                if (FAILED(hrc))
                    throw hrc;
                continue;
            }

            // Skip over the 'ovf:/disk/' prefix.
            const Utf8Str strDiskID = strHostResource.substr(10);
            const VirtualSystemDescriptionEntry *pDiskEntry = stack.mapDisks[strDiskID];

            // source path: where the VBox image is
            const Utf8Str &strSrcFilePath = pDiskEntry->strVBoxCurrent;

            //skip empty Medium. In common, It's may be empty CD/DVD
            if (strSrcFilePath.isEmpty() ||
                pDiskEntry->skipIt == true)
                continue;

            // Do NOT check here whether the file exists. findHardDisk will
            // figure that out, and filesystem-based tests are simply wrong
            // in the general case (think of iSCSI).

            // clone the disk:
            ComObjPtr<Medium> pSourceDisk;

            Log(("Finding source disk \"%s\"\n", strSrcFilePath.c_str()));

            if (pDiskEntry->type == VirtualSystemDescriptionType_HardDiskImage)
            {
                hrc = mVirtualBox->i_findHardDiskByLocation(strSrcFilePath, true, &pSourceDisk);
                if (FAILED(hrc)) throw hrc;
            }
            else//may be CD or DVD
            {
                hrc = mVirtualBox->i_findDVDOrFloppyImage(DeviceType_DVD,
                                                          NULL,
                                                          strSrcFilePath,
                                                          true,
                                                          &pSourceDisk);
                if (FAILED(hrc)) throw hrc;
            }

            Bstr uuidSource;
            hrc = pSourceDisk->COMGETTER(Id)(uuidSource.asOutParam());
            if (FAILED(hrc)) throw hrc;

            // output filename
            const Utf8Str &strTargetFileNameOnly = pDiskEntry->strOvf;

            // The exporting requests a lock on the media tree. So leave our lock temporary.
            writeLock.release();
            try
            {
                // advance to the next operation
                pTask->pProgress->SetNextOperation(BstrFmt(tr("Exporting to disk image '%s'"),
                                                           RTPathFilename(strTargetFileNameOnly.c_str())).raw(),
                                                   pDiskEntry->ulSizeMB);     // operation's weight, as set up
                                                                              // with the IProgress originally

                // create a flat copy of the source disk image
                if (pDiskEntry->type == VirtualSystemDescriptionType_HardDiskImage)
                {
                    /*
                     * Export a disk image.
                     */
                    /* For compressed VMDK fun, we let i_exportFile produce the image bytes. */
                    RTVFSIOSTREAM hVfsIosDst;
                    vrc = RTVfsFsStrmPushFile(hVfsFssDst, strTargetFileNameOnly.c_str(), UINT64_MAX,
                                              NULL /*paObjInfo*/, 0 /*cObjInfo*/, RTVFSFSSTRM_PUSH_F_STREAM, &hVfsIosDst);
                    if (RT_FAILURE(vrc))
                        throw setErrorVrc(vrc, tr("RTVfsFsStrmPushFile failed for '%s' (%Rrc)"), strTargetFileNameOnly.c_str(), vrc);
                    hVfsIosDst = i_manifestSetupDigestCalculationForGivenIoStream(hVfsIosDst, strTargetFileNameOnly.c_str(),
                                                                                  false /*fRead*/);
                    if (hVfsIosDst == NIL_RTVFSIOSTREAM)
                        throw setError(E_FAIL, "i_manifestSetupDigestCalculationForGivenIoStream(%s)", strTargetFileNameOnly.c_str());

                    hrc = pSourceDisk->i_exportFile(strTargetFileNameOnly.c_str(),
                                                    format,
                                                    MediumVariant_VmdkStreamOptimized,
                                                    m->m_pSecretKeyStore,
                                                    hVfsIosDst,
                                                    pTask->pProgress);
                    RTVfsIoStrmRelease(hVfsIosDst);
                }
                else
                {
                    /*
                     * Copy CD/DVD/floppy image.
                     */
                    Assert(pDiskEntry->type == VirtualSystemDescriptionType_CDROM);
                    hrc = pSourceDisk->i_addRawToFss(strTargetFileNameOnly.c_str(), m->m_pSecretKeyStore, hVfsFssDst,
                                                    pTask->pProgress, false /*fSparse*/);
                }
                if (FAILED(hrc)) throw hrc;
            }
            catch (HRESULT rc3)
            {
                writeLock.acquire();
                /// @todo file deletion on error? If not, we can remove that whole try/catch block.
                throw rc3;
            }
            // Finished, lock again (so nobody mess around with the medium tree
            // in the meantime)
            writeLock.acquire();
        }

        if (m->fManifest)
        {
            // Create & write the manifest file
            Utf8Str strMfFilePath = Utf8Str(pTask->locInfo.strPath).stripSuffix().append(".mf");
            Utf8Str strMfFileName = Utf8Str(strMfFilePath).stripPath();
            pTask->pProgress->SetNextOperation(BstrFmt(tr("Creating manifest file '%s'"), strMfFileName.c_str()).raw(),
                                               m->ulWeightForManifestOperation);     // operation's weight, as set up
                                                                                     // with the IProgress originally);
            /* Create a memory I/O stream and write the manifest to it. */
            RTVFSIOSTREAM hVfsIosManifest;
            vrc = RTVfsMemIoStrmCreate(NIL_RTVFSIOSTREAM, _1K, &hVfsIosManifest);
            if (RT_FAILURE(vrc))
                throw setErrorVrc(vrc, tr("RTVfsMemIoStrmCreate failed (%Rrc)"), vrc);
            if (m->hOurManifest != NIL_RTMANIFEST) /* In case it's empty. */
                vrc = RTManifestWriteStandard(m->hOurManifest, hVfsIosManifest);
            if (RT_SUCCESS(vrc))
            {
                /* Rewind the stream and add it to the output. */
                size_t cbIgnored;
                vrc = RTVfsIoStrmReadAt(hVfsIosManifest, 0 /*offset*/, &cbIgnored, 0, true /*fBlocking*/, &cbIgnored);
                if (RT_SUCCESS(vrc))
                {
                    RTVFSOBJ hVfsObjManifest = RTVfsObjFromIoStream(hVfsIosManifest);
                    vrc = RTVfsFsStrmAdd(hVfsFssDst, strMfFileName.c_str(), hVfsObjManifest, 0 /*fFlags*/);
                    if (RT_SUCCESS(vrc))
                        hrc = S_OK;
                    else
                        hrc = setErrorVrc(vrc, tr("RTVfsFsStrmAdd failed for the manifest (%Rrc)"), vrc);
                }
                else
                    hrc = setErrorVrc(vrc, tr("RTManifestWriteStandard failed (%Rrc)"), vrc);
            }
            else
                hrc = setErrorVrc(vrc, tr("RTManifestWriteStandard failed (%Rrc)"), vrc);
            RTVfsIoStrmRelease(hVfsIosManifest);
            if (FAILED(hrc))
                throw hrc;
        }
    }
    catch (RTCError &x)  // includes all XML exceptions
    {
        hrc = setError(VBOX_E_FILE_ERROR, x.what());
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    LogFlowFuncLeave();

    return hrc;
}


/**
 * Writes a memory buffer to a file in the output file system stream.
 *
 * @returns COM status code.
 * @param   hVfsFssDst      The file system stream to add the file to.
 * @param   pszFilename     The file name (w/ path if desired).
 * @param   pvContent       Pointer to buffer containing the file content.
 * @param   cbContent       Size of the content.
 */
HRESULT Appliance::i_writeBufferToFile(RTVFSFSSTREAM hVfsFssDst, const char *pszFilename, const void *pvContent, size_t cbContent)
{
    /*
     * Create a VFS file around the memory, converting it to a base VFS object handle.
     */
    HRESULT hrc;
    RTVFSIOSTREAM hVfsIosSrc;
    int vrc = RTVfsIoStrmFromBuffer(RTFILE_O_READ, pvContent, cbContent, &hVfsIosSrc);
    if (RT_SUCCESS(vrc))
    {
        hVfsIosSrc = i_manifestSetupDigestCalculationForGivenIoStream(hVfsIosSrc, pszFilename);
        AssertReturn(hVfsIosSrc != NIL_RTVFSIOSTREAM,
                     setErrorVrc(vrc, "i_manifestSetupDigestCalculationForGivenIoStream"));

        RTVFSOBJ hVfsObj = RTVfsObjFromIoStream(hVfsIosSrc);
        RTVfsIoStrmRelease(hVfsIosSrc);
        AssertReturn(hVfsObj != NIL_RTVFSOBJ, E_FAIL);

        /*
         * Add it to the stream.
         */
        vrc = RTVfsFsStrmAdd(hVfsFssDst, pszFilename, hVfsObj, 0);
        RTVfsObjRelease(hVfsObj);
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else
            hrc = setErrorVrc(vrc, tr("RTVfsFsStrmAdd failed for '%s' (%Rrc)"), pszFilename, vrc);
    }
    else
        hrc = setErrorVrc(vrc, "RTVfsIoStrmFromBuffer");
    return hrc;
}


/**
 * Writes an NVRAM file to the output file system stream.
 *
 * @returns COM status code.
 * @param   strNvramSrcPath The path to the NVRAM file.
 * @param   strNvramDstFile The name of the output file to store in the stream.
 * @param   hVfsFssDst      The file system stream to add the file to.
 */
HRESULT Appliance::i_writeNvramFileToIoStream(Utf8Str strNvramSrcPath, Utf8Str strNvramDstFile, RTVFSFSSTREAM hVfsFssDst)
{
    HRESULT hrc = S_OK;
    RTVFSIOSTREAM hVfsIosSrc = NIL_RTVFSIOSTREAM;
    int vrc = RTVfsChainOpenIoStream(strNvramSrcPath.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                     &hVfsIosSrc, NULL /*poffError*/, NULL);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("RTVfsChainOpenIoStream() failed for '%s' (%Rrc)"), strNvramSrcPath.c_str(), vrc);

    RTVFSIOSTREAM hVfsIosDst;
    vrc = RTVfsFsStrmPushFile(hVfsFssDst, strNvramDstFile.c_str(), UINT64_MAX,
                              NULL /*paObjInfo*/, 0 /*cObjInfo*/, RTVFSFSSTRM_PUSH_F_STREAM, &hVfsIosDst);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("RTVfsFsStrmPushFile() failed for '%s' (%Rrc)"), strNvramDstFile.c_str(), vrc);

    hVfsIosDst = i_manifestSetupDigestCalculationForGivenIoStream(hVfsIosDst, strNvramDstFile.c_str(),
                                                                  false /*fRead*/);
    if (hVfsIosDst == NIL_RTVFSIOSTREAM)
        return setError(E_FAIL, "i_manifestSetupDigestCalculationForGivenIoStream(%s)", strNvramDstFile.c_str());

    vrc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, 0);
    if (RT_FAILURE(vrc))
        return setErrorVrc(vrc, tr("RTVfsUtilPumpIoStreams() failed for '%s' (%Rrc)"), strNvramDstFile.c_str(), vrc);

    RTVfsIoStrmRelease(hVfsIosSrc);
    RTVfsIoStrmRelease(hVfsIosDst);

    return hrc;
}

