/* $Id: tstOVF.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * tstOVF - testcases for OVF import and export
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

#include <VBox/com/VirtualBox.h>

#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include <VBox/com/string.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/param.h>

#include <list>

using namespace com;

// main
///////////////////////////////////////////////////////////////////////////////

/**
 * Quick hack exception structure.
 *
 */
struct MyError
{
    MyError(HRESULT rc,
            const char *pcsz,
            IProgress *pProgress = NULL)
        : m_rc(rc)
    {
        m_str = "ERROR: ";
        m_str += pcsz;

        if (pProgress)
        {
            com::ProgressErrorInfo info(pProgress);
            com::GluePrintErrorInfo(info);
        }
        else if (rc != S_OK)
        {
            com::ErrorInfo info;
            if (!info.isFullAvailable() && !info.isBasicAvailable())
                com::GluePrintRCMessage(rc);
            else
                com::GluePrintErrorInfo(info);
        }
    }

    Utf8Str m_str;
    HRESULT m_rc;
};

/**
 * Imports the given OVF file, with all bells and whistles.
 * Throws MyError on errors.
 * @param pcszPrefix Descriptive short prefix string for console output.
 * @param pVirtualBox VirtualBox instance.
 * @param pcszOVF0 File to import.
 * @param llMachinesCreated out: UUIDs of machines that were created so that caller can clean up.
 */
static void importOVF(const char *pcszPrefix,
                      ComPtr<IVirtualBox> &pVirtualBox,
                      const char *pcszOVF0,
                      std::list<Guid> &llMachinesCreated)
{
    char szAbsOVF[RTPATH_MAX];
    RTPathExecDir(szAbsOVF, sizeof(szAbsOVF));
    RTPathAppend(szAbsOVF, sizeof(szAbsOVF), pcszOVF0);

    RTPrintf("%s: reading appliance \"%s\"...\n", pcszPrefix, szAbsOVF);
    ComPtr<IAppliance> pAppl;
    HRESULT rc = pVirtualBox->CreateAppliance(pAppl.asOutParam());
    if (FAILED(rc)) throw MyError(rc, "failed to create appliance\n");

    ComPtr<IProgress> pProgress;
    rc = pAppl->Read(Bstr(szAbsOVF).raw(), pProgress.asOutParam());
    if (FAILED(rc)) throw MyError(rc, "Appliance::Read() failed\n");
    rc = pProgress->WaitForCompletion(-1);
    if (FAILED(rc)) throw MyError(rc, "Progress::WaitForCompletion() failed\n");
    LONG rc2;
    pProgress->COMGETTER(ResultCode)(&rc2);
    if (FAILED(rc2)) throw MyError(rc2, "Appliance::Read() failed\n", pProgress);

    RTPrintf("%s: interpreting appliance \"%s\"...\n", pcszPrefix, szAbsOVF);
    rc = pAppl->Interpret();
    if (FAILED(rc)) throw MyError(rc, "Appliance::Interpret() failed\n");

    com::SafeIfaceArray<IVirtualSystemDescription> aDescriptions;
    rc = pAppl->COMGETTER(VirtualSystemDescriptions)(ComSafeArrayAsOutParam(aDescriptions));
    for (uint32_t u = 0;
         u < aDescriptions.size();
         ++u)
    {
        ComPtr<IVirtualSystemDescription> pVSys = aDescriptions[u];

        com::SafeArray<VirtualSystemDescriptionType_T> aTypes;
        com::SafeArray<BSTR> aRefs;
        com::SafeArray<BSTR> aOvfValues;
        com::SafeArray<BSTR> aVBoxValues;
        com::SafeArray<BSTR> aExtraConfigValues;
        rc = pVSys->GetDescription(ComSafeArrayAsOutParam(aTypes),
                                   ComSafeArrayAsOutParam(aRefs),
                                   ComSafeArrayAsOutParam(aOvfValues),
                                   ComSafeArrayAsOutParam(aVBoxValues),
                                   ComSafeArrayAsOutParam(aExtraConfigValues));
        if (FAILED(rc)) throw MyError(rc, "VirtualSystemDescription::GetDescription() failed\n");

        for (uint32_t u2 = 0;
             u2 < aTypes.size();
             ++u2)
        {
            const char *pcszType;

            VirtualSystemDescriptionType_T t = aTypes[u2];
            switch (t)
            {
                case VirtualSystemDescriptionType_OS:
                    pcszType = "ostype";
                break;

                case VirtualSystemDescriptionType_Name:
                    pcszType = "name";
                break;

                case VirtualSystemDescriptionType_Product:
                    pcszType = "product";
                break;

                case VirtualSystemDescriptionType_ProductUrl:
                    pcszType = "producturl";
                break;

                case VirtualSystemDescriptionType_Vendor:
                    pcszType = "vendor";
                break;

                case VirtualSystemDescriptionType_VendorUrl:
                    pcszType = "vendorurl";
                break;

                case VirtualSystemDescriptionType_Version:
                    pcszType = "version";
                break;

                case VirtualSystemDescriptionType_Description:
                    pcszType = "description";
                break;

                case VirtualSystemDescriptionType_License:
                    pcszType = "license";
                break;

                case VirtualSystemDescriptionType_CPU:
                    pcszType = "cpu";
                break;

                case VirtualSystemDescriptionType_Memory:
                    pcszType = "memory";
                break;

                case VirtualSystemDescriptionType_HardDiskControllerIDE:
                    pcszType = "ide";
                break;

                case VirtualSystemDescriptionType_HardDiskControllerSATA:
                    pcszType = "sata";
                break;

                case VirtualSystemDescriptionType_HardDiskControllerSAS:
                    pcszType = "sas";
                break;

                case VirtualSystemDescriptionType_HardDiskControllerSCSI:
                    pcszType = "scsi";
                break;

                case VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI:
                    pcszType = "virtio-scsi";
                break;

                case VirtualSystemDescriptionType_HardDiskControllerNVMe:
                    pcszType = "nvme";
                break;

                case VirtualSystemDescriptionType_HardDiskImage:
                    pcszType = "hd";
                break;

                case VirtualSystemDescriptionType_CDROM:
                    pcszType = "cdrom";
                break;

                case VirtualSystemDescriptionType_Floppy:
                    pcszType = "floppy";
                break;

                case VirtualSystemDescriptionType_NetworkAdapter:
                    pcszType = "net";
                break;

                case VirtualSystemDescriptionType_USBController:
                    pcszType = "usb";
                break;

                case VirtualSystemDescriptionType_SoundCard:
                    pcszType = "sound";
                break;

                case VirtualSystemDescriptionType_SettingsFile:
                    pcszType = "settings";
                break;

                case VirtualSystemDescriptionType_BaseFolder:
                    pcszType = "basefolder";
                break;

                case VirtualSystemDescriptionType_PrimaryGroup:
                    pcszType = "primarygroup";
                break;

                default:
                    throw MyError(E_UNEXPECTED, Utf8StrFmt("Invalid VirtualSystemDescriptionType (enum=%d)\n", t).c_str());
                break;
            }

            RTPrintf("  vsys %2u item %2u: type %2d (%s), ovf: \"%ls\", vbox: \"%ls\", extra: \"%ls\"\n",
                     u, u2, t, pcszType,
                     aOvfValues[u2],
                     aVBoxValues[u2],
                     aExtraConfigValues[u2]);
        }
    }

    RTPrintf("%s: importing %d machine(s)...\n", pcszPrefix, aDescriptions.size());
    SafeArray<ImportOptions_T> sfaOptions;
    rc = pAppl->ImportMachines(ComSafeArrayAsInParam(sfaOptions), pProgress.asOutParam());
    if (FAILED(rc)) throw MyError(rc, "Appliance::ImportMachines() failed\n");
    rc = pProgress->WaitForCompletion(-1);
    if (FAILED(rc)) throw MyError(rc, "Progress::WaitForCompletion() failed\n");
    pProgress->COMGETTER(ResultCode)(&rc2);
    if (FAILED(rc2)) throw MyError(rc2, "Appliance::ImportMachines() failed\n", pProgress);

    com::SafeArray<BSTR> aMachineUUIDs;
    rc = pAppl->COMGETTER(Machines)(ComSafeArrayAsOutParam(aMachineUUIDs));
    if (FAILED(rc)) throw MyError(rc, "Appliance::GetMachines() failed\n");

    for (size_t u = 0;
         u < aMachineUUIDs.size();
         ++u)
    {
        RTPrintf("%s: created machine %u: %ls\n", pcszPrefix, u, aMachineUUIDs[u]);
        llMachinesCreated.push_back(Guid(Bstr(aMachineUUIDs[u])));
    }

    RTPrintf("%s: success!\n", pcszPrefix);
}

/**
 * Copies ovf-testcases/ovf-dummy.vmdk to the given target and appends that
 * target as a string to the given list so that the caller can delete it
 * again later.
 * @param llFiles2Delete List of strings to append the target file path to.
 * @param pcszDest Target for dummy VMDK.
 */
static void copyDummyDiskImage(const char *pcszPrefix,
                               std::list<Utf8Str> &llFiles2Delete,
                               const char *pcszDest)
{
    char szSrc[RTPATH_MAX];
    RTPathExecDir(szSrc, sizeof(szSrc));
    RTPathAppend(szSrc, sizeof(szSrc), "ovf-testcases/ovf-dummy.vmdk");

    char szDst[RTPATH_MAX];
    RTPathExecDir(szDst, sizeof(szDst));
    RTPathAppend(szDst, sizeof(szDst), pcszDest);
    RTPrintf("%s: copying ovf-dummy.vmdk to \"%s\"...\n", pcszPrefix, pcszDest);

    /* Delete the destination file if it exists or RTFileCopy will fail. */
    if (RTFileExists(szDst))
    {
        RTPrintf("Deleting file %s...\n", szDst);
        RTFileDelete(szDst);
    }

    int vrc = RTFileCopy(szSrc, szDst);
    if (RT_FAILURE(vrc)) throw MyError(0, Utf8StrFmt("Cannot copy ovf-dummy.vmdk to %s: %Rra\n", pcszDest, vrc).c_str());
    llFiles2Delete.push_back(szDst);
}

/**
 *
 * @param argc
 * @param argv[]
 * @return
 */
int main(int argc, char *argv[])
{
    RTR3InitExe(argc, &argv, 0);

    /** @todo r=aeichner: This should work for x86 machines on ARM platforms as well.
     * Currently we get:
     *     tstOVF: error: x86-specific platform settings are not available on this platform
     *     tstOVF: error: Details: code VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED (0x80bb0012), component PlatformWrap, interface IPlatform
     * */

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    HRESULT rc = S_OK;

    std::list<Utf8Str> llFiles2Delete;
    std::list<Guid> llMachinesCreated;

    ComPtr<IVirtualBoxClient> pVirtualBoxClient;
    ComPtr<IVirtualBox> pVirtualBox;

    try
    {
        RTPrintf("Initializing COM...\n");
        rc = com::Initialize();
        if (FAILED(rc)) throw MyError(rc, "failed to initialize COM!\n");

        ComPtr<ISession> pSession;

        RTPrintf("Creating VirtualBox object...\n");
        rc = pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
        if (SUCCEEDED(rc))
            rc = pVirtualBoxClient->COMGETTER(VirtualBox)(pVirtualBox.asOutParam());
        if (FAILED(rc)) throw MyError(rc, "failed to create the VirtualBox object!\n");

        rc = pSession.createInprocObject(CLSID_Session);
        if (FAILED(rc)) throw MyError(rc, "failed to create a session object!\n");

        // for each testcase, we will copy the dummy VMDK image to the subdirectory with the OVF testcase
        // so that the import will find the disks it expects; this is just for testing the import since
        // the imported machines will obviously not be usable.
        // llFiles2Delete receives the paths of all the files that we need to clean up later.

        // testcase 1: import ovf-joomla-0.9/joomla-1.1.4-ovf.ovf
        copyDummyDiskImage("joomla-0.9", llFiles2Delete, "ovf-testcases/ovf-joomla-0.9/joomla-1.1.4-ovf-0.vmdk");
        copyDummyDiskImage("joomla-0.9", llFiles2Delete, "ovf-testcases/ovf-joomla-0.9/joomla-1.1.4-ovf-1.vmdk");
        importOVF("joomla-0.9", pVirtualBox, "ovf-testcases/ovf-joomla-0.9/joomla-1.1.4-ovf.ovf", llMachinesCreated);

        // testcase 2: import ovf-winxp-vbox-sharedfolders/winxp.ovf
        copyDummyDiskImage("winxp-vbox-sharedfolders", llFiles2Delete, "ovf-testcases/ovf-winxp-vbox-sharedfolders/Windows 5.1 XP 1 merged.vmdk");
        copyDummyDiskImage("winxp-vbox-sharedfolders", llFiles2Delete, "ovf-testcases/ovf-winxp-vbox-sharedfolders/smallvdi.vmdk");
        importOVF("winxp-vbox-sharedfolders", pVirtualBox, "ovf-testcases/ovf-winxp-vbox-sharedfolders/winxp.ovf", llMachinesCreated);

        // testcase 3: import ovf-winxp-vbox-sharedfolders/winxp.ovf
        importOVF("winhost-audio-nodisks", pVirtualBox, "ovf-testcases/ovf-winhost-audio-nodisks/WinXP.ovf", llMachinesCreated);

        RTPrintf("Machine imports done, no errors. Cleaning up...\n");
    }
    catch (MyError &e)
    {
        rc = e.m_rc;
        RTPrintf("%s", e.m_str.c_str());
        rcExit = RTEXITCODE_FAILURE;
    }

    try
    {
        // clean up the machines created
        for (std::list<Guid>::const_iterator it = llMachinesCreated.begin();
             it != llMachinesCreated.end();
             ++it)
        {
            const Guid &uuid = *it;
            Bstr bstrUUID(uuid.toUtf16());
            ComPtr<IMachine> pMachine;
            rc = pVirtualBox->FindMachine(bstrUUID.raw(), pMachine.asOutParam());
            if (FAILED(rc)) throw MyError(rc, "VirtualBox::FindMachine() failed\n");

            RTPrintf("  Deleting machine %ls...\n", bstrUUID.raw());
            SafeIfaceArray<IMedium> sfaMedia;
            rc = pMachine->Unregister(CleanupMode_DetachAllReturnHardDisksOnly,
                                      ComSafeArrayAsOutParam(sfaMedia));
            if (FAILED(rc)) throw MyError(rc, "Machine::Unregister() failed\n");

            ComPtr<IProgress> pProgress;
            rc = pMachine->DeleteConfig(ComSafeArrayAsInParam(sfaMedia), pProgress.asOutParam());
            if (FAILED(rc)) throw MyError(rc, "Machine::DeleteSettings() failed\n");
            rc = pProgress->WaitForCompletion(-1);
            if (FAILED(rc)) throw MyError(rc, "Progress::WaitForCompletion() failed\n");
        }
    }
    catch (MyError &e)
    {
        rc = e.m_rc;
        RTPrintf("%s", e.m_str.c_str());
        rcExit = RTEXITCODE_FAILURE;
    }

    // clean up the VMDK copies that we made in copyDummyDiskImage()
    for (std::list<Utf8Str>::const_iterator it = llFiles2Delete.begin();
         it != llFiles2Delete.end();
         ++it)
    {
        const Utf8Str &strFile = *it;
        RTPrintf("Deleting file %s...\n", strFile.c_str());
        RTFileDelete(strFile.c_str());
    }

    pVirtualBox.setNull();
    pVirtualBoxClient.setNull();

    RTPrintf("Shutting down COM...\n");
    com::Shutdown();
    RTPrintf("tstOVF all done: %s\n", rcExit ? "ERROR" : "SUCCESS");

    return rcExit;
}

