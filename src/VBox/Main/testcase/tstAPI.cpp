/* $Id: tstAPI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstAPI - test program for our COM/XPCOM interface
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

#include <stdio.h>
#include <stdlib.h>

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/VirtualBox.h>

using namespace com;

#define LOG_ENABLED
#define LOG_GROUP LOG_GROUP_MAIN
#include <VBox/log.h>

#include <iprt/initterm.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/stream.h>
#include <iprt/thread.h>


// forward declarations
///////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_RESOURCE_USAGE_API
static Bstr getObjectName(ComPtr<IVirtualBox> aVirtualBox, ComPtr<IUnknown> aObject);
static void queryMetrics(ComPtr<IVirtualBox> aVirtualBox,
                         ComPtr<IPerformanceCollector> collector,
                         ComSafeArrayIn(IUnknown *, objects));
static void listAffectedMetrics(ComPtr<IVirtualBox> aVirtualBox,
                                ComSafeArrayIn(IPerformanceMetric*, aMetrics));
#endif

// funcs
///////////////////////////////////////////////////////////////////////////////

#if 0 /* unused */
static HRESULT readAndChangeMachineSettings(IMachine *machine, IMachine *readonlyMachine = 0)
{
    HRESULT hrc = S_OK;

    Bstr name;
    RTPrintf("Getting machine name...\n");
    CHECK_ERROR_RET(machine, COMGETTER(Name)(name.asOutParam()), hrc);
    RTPrintf("Name: {%ls}\n", name.raw());

    RTPrintf("Getting machine GUID...\n");
    Bstr guid;
    CHECK_ERROR(machine, COMGETTER(Id)(guid.asOutParam()));
    if (SUCCEEDED(hrc) && !guid.isEmpty()) {
        RTPrintf("Guid::toString(): {%s}\n", Utf8Str(guid).c_str());
    } else {
        RTPrintf("WARNING: there's no GUID!");
    }

    ULONG memorySize;
    RTPrintf("Getting memory size...\n");
    CHECK_ERROR_RET(machine, COMGETTER(MemorySize)(&memorySize), hrc);
    RTPrintf("Memory size: %d\n", memorySize);

    MachineState_T machineState;
    RTPrintf("Getting machine state...\n");
    CHECK_ERROR_RET(machine, COMGETTER(State)(&machineState), hrc);
    RTPrintf("Machine state: %d\n", machineState);

    BOOL modified;
    RTPrintf("Are any settings modified?...\n");
    CHECK_ERROR(machine, COMGETTER(SettingsModified)(&modified));
    if (SUCCEEDED(hrc))
        RTPrintf("%s\n", modified ? "yes" : "no");

    ULONG memorySizeBig = memorySize * 10;
    RTPrintf("Changing memory size to %d...\n", memorySizeBig);
    CHECK_ERROR(machine, COMSETTER(MemorySize)(memorySizeBig));

    if (SUCCEEDED(hrc))
    {
        RTPrintf("Are any settings modified now?...\n");
        CHECK_ERROR_RET(machine, COMGETTER(SettingsModified)(&modified), hrc);
        RTPrintf("%s\n", modified ? "yes" : "no");
        ASSERT_RET(modified, 0);

        ULONG memorySizeGot;
        RTPrintf("Getting memory size again...\n");
        CHECK_ERROR_RET(machine, COMGETTER(MemorySize)(&memorySizeGot), hrc);
        RTPrintf("Memory size: %d\n", memorySizeGot);
        ASSERT_RET(memorySizeGot == memorySizeBig, 0);

        if (readonlyMachine)
        {
            RTPrintf("Getting memory size of the counterpart readonly machine...\n");
            ULONG memorySizeRO;
            readonlyMachine->COMGETTER(MemorySize)(&memorySizeRO);
            RTPrintf("Memory size: %d\n", memorySizeRO);
            ASSERT_RET(memorySizeRO != memorySizeGot, 0);
        }

        RTPrintf("Discarding recent changes...\n");
        CHECK_ERROR_RET(machine, DiscardSettings(), hrc);
        RTPrintf("Are any settings modified after discarding?...\n");
        CHECK_ERROR_RET(machine, COMGETTER(SettingsModified)(&modified), hrc);
        RTPrintf("%s\n", modified ? "yes" : "no");
        ASSERT_RET(!modified, 0);

        RTPrintf("Getting memory size once more...\n");
        CHECK_ERROR_RET(machine, COMGETTER(MemorySize)(&memorySizeGot), hrc);
        RTPrintf("Memory size: %d\n", memorySizeGot);
        ASSERT_RET(memorySizeGot == memorySize, 0);

        memorySize = memorySize > 128 ? memorySize / 2 : memorySize * 2;
        RTPrintf("Changing memory size to %d...\n", memorySize);
        CHECK_ERROR_RET(machine, COMSETTER(MemorySize)(memorySize), hrc);
    }

    Bstr desc;
    RTPrintf("Getting description...\n");
    CHECK_ERROR_RET(machine, COMGETTER(Description)(desc.asOutParam()), hrc);
    RTPrintf("Description is: \"%ls\"\n", desc.raw());

    desc = L"This is an exemplary description (changed).";
    RTPrintf("Setting description to \"%ls\"...\n", desc.raw());
    CHECK_ERROR_RET(machine, COMSETTER(Description)(desc.raw()), hrc);

    RTPrintf("Saving machine settings...\n");
    CHECK_ERROR(machine, SaveSettings());
    if (SUCCEEDED(hrc))
    {
        RTPrintf("Are any settings modified after saving?...\n");
        CHECK_ERROR_RET(machine, COMGETTER(SettingsModified)(&modified), hrc);
        RTPrintf("%s\n", modified ? "yes" : "no");
        ASSERT_RET(!modified, 0);

        if (readonlyMachine) {
            RTPrintf("Getting memory size of the counterpart readonly machine...\n");
            ULONG memorySizeRO;
            readonlyMachine->COMGETTER(MemorySize)(&memorySizeRO);
            RTPrintf("Memory size: %d\n", memorySizeRO);
            ASSERT_RET(memorySizeRO == memorySize, 0);
        }
    }

    Bstr extraDataKey = L"Blafasel";
    Bstr extraData;
    RTPrintf("Getting extra data key {%ls}...\n", extraDataKey.raw());
    CHECK_ERROR_RET(machine, GetExtraData(extraDataKey.raw(), extraData.asOutParam()), hrc);
    if (!extraData.isEmpty()) {
        RTPrintf("Extra data value: {%ls}\n", extraData.raw());
    } else {
        RTPrintf("No extra data exists\n");
    }

    if (extraData.isEmpty())
        extraData = L"Das ist die Berliner Luft, Luft, Luft...";
    else
        extraData.setNull();
    RTPrintf("Setting extra data key {%ls} to {%ls}...\n",
             extraDataKey.raw(), extraData.raw());
    CHECK_ERROR(machine, SetExtraData(extraDataKey.raw(), extraData.raw()));

    if (SUCCEEDED(hrc)) {
        RTPrintf("Getting extra data key {%ls} again...\n", extraDataKey.raw());
        CHECK_ERROR_RET(machine, GetExtraData(extraDataKey.raw(), extraData.asOutParam()), hrc);
        if (!extraData.isEmpty()) {
            RTPrintf("Extra data value: {%ls}\n", extraData.raw());
        } else {
            RTPrintf("No extra data exists\n");
        }
    }

    return hrc;
}
#endif

// main
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    /*
     * Initialize the VBox runtime without loading
     * the support driver.
     */
    RTR3InitExe(argc, &argv, 0);

    HRESULT hrc;

    {
        char homeDir[RTPATH_MAX];
        GetVBoxUserHomeDirectory(homeDir, sizeof(homeDir));
        RTPrintf("VirtualBox Home Directory = '%s'\n", homeDir);
    }

    RTPrintf("Initializing COM...\n");

    hrc = com::Initialize();
    if (FAILED(hrc))
    {
        RTPrintf("ERROR: failed to initialize COM!\n");
        return hrc;
    }

    do
    {
    // scopes all the stuff till shutdown
    ////////////////////////////////////////////////////////////////////////////

    ComPtr<IVirtualBoxClient> virtualBoxClient;
    ComPtr<IVirtualBox> virtualBox;
    ComPtr<ISession> session;

#if 0
    // Utf8Str test
    ////////////////////////////////////////////////////////////////////////////

    Utf8Str nullUtf8Str;
    RTPrintf("nullUtf8Str='%s'\n", nullUtf8Str.raw());

    Utf8Str simpleUtf8Str = "simpleUtf8Str";
    RTPrintf("simpleUtf8Str='%s'\n", simpleUtf8Str.raw());

    Utf8Str utf8StrFmt = Utf8StrFmt("[0=%d]%s[1=%d]", 0, "utf8StrFmt", 1);
    RTPrintf("utf8StrFmt='%s'\n", utf8StrFmt.raw());

#endif

    RTPrintf("Creating VirtualBox object...\n");
    hrc = virtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
    if (SUCCEEDED(hrc))
        hrc = virtualBoxClient->COMGETTER(VirtualBox)(virtualBox.asOutParam());
    if (FAILED(hrc))
        RTPrintf("ERROR: failed to create the VirtualBox object!\n");
    else
    {
        hrc = session.createInprocObject(CLSID_Session);
        if (FAILED(hrc))
            RTPrintf("ERROR: failed to create a session object!\n");
    }

    if (FAILED(hrc))
    {
        com::ErrorInfo info;
        if (!info.isFullAvailable() && !info.isBasicAvailable())
        {
            com::GluePrintRCMessage(hrc);
            RTPrintf("Most likely, the VirtualBox COM server is not running or failed to start.\n");
        }
        else
            com::GluePrintErrorInfo(info);
        break;
    }

#if 0
    // Testing VirtualBox::COMGETTER(ProgressOperations).
    // This is designed to be tested while running
    // "./VBoxManage clonehd src.vdi clone.vdi" in parallel.
    // It will then display the progress every 2 seconds.
    ////////////////////////////////////////////////////////////////////////////
    {
        RTPrintf("Testing VirtualBox::COMGETTER(ProgressOperations)...\n");

        for (;;) {
            com::SafeIfaceArray<IProgress> operations;

            CHECK_ERROR_BREAK(virtualBox,
                COMGETTER(ProgressOperations)(ComSafeArrayAsOutParam(operations)));

            RTPrintf("operations: %d\n", operations.size());
            if (operations.size() == 0)
                break; // No more operations left.

            for (size_t i = 0; i < operations.size(); ++i) {
                PRInt32 percent;

                operations[i]->COMGETTER(Percent)(&percent);
                RTPrintf("operations[%u]: %ld\n", (unsigned)i, (long)percent);
            }
            RTThreadSleep(2000); // msec
        }
    }
#endif

#if 0
    // IUnknown identity test
    ////////////////////////////////////////////////////////////////////////////
    {
        {
            ComPtr<IVirtualBox> virtualBox2;

            RTPrintf("Creating one more VirtualBox object...\n");
            CHECK_RC(virtualBoxClient->COMGETTER(virtualBox2.asOutParam()));
            if (FAILED(rc))
            {
                CHECK_ERROR_NOCALL();
                break;
            }

            RTPrintf("IVirtualBox(virtualBox)=%p IVirtualBox(virtualBox2)=%p\n",
                     (IVirtualBox *)virtualBox, (IVirtualBox *)virtualBox2);
            Assert((IVirtualBox *)virtualBox == (IVirtualBox *)virtualBox2);

            ComPtr<IUnknown> unk(virtualBox);
            ComPtr<IUnknown> unk2;
            unk2 = virtualBox2;

            RTPrintf("IUnknown(virtualBox)=%p IUnknown(virtualBox2)=%p\n",
                     (IUnknown *)unk, (IUnknown *)unk2);
            Assert((IUnknown *)unk == (IUnknown *)unk2);

            ComPtr<IVirtualBox> vb = unk;
            ComPtr<IVirtualBox> vb2 = unk;

            RTPrintf("IVirtualBox(IUnknown(virtualBox))=%p IVirtualBox(IUnknown(virtualBox2))=%p\n",
                    (IVirtualBox *)vb, (IVirtualBox *)vb2);
            Assert((IVirtualBox *)vb == (IVirtualBox *)vb2);
        }

        {
            ComPtr<IHost> host;
            CHECK_ERROR_BREAK(virtualBox, COMGETTER(Host)(host.asOutParam()));
            RTPrintf(" IHost(host)=%p\n", (IHost *)host);
            ComPtr<IUnknown> unk = host;
            RTPrintf(" IUnknown(host)=%p\n", (IUnknown *)unk);
            ComPtr<IHost> host_copy = unk;
            RTPrintf(" IHost(host_copy)=%p\n", (IHost *)host_copy);
            ComPtr<IUnknown> unk_copy = host_copy;
            RTPrintf(" IUnknown(host_copy)=%p\n", (IUnknown *)unk_copy);
            Assert((IUnknown *)unk == (IUnknown *)unk_copy);

            /* query IUnknown on IUnknown */
            ComPtr<IUnknown> unk_copy_copy;
            unk_copy.queryInterfaceTo(unk_copy_copy.asOutParam());
            RTPrintf(" IUnknown(unk_copy)=%p\n", (IUnknown *)unk_copy_copy);
            Assert((IUnknown *)unk_copy == (IUnknown *)unk_copy_copy);
            /* query IUnknown on IUnknown in the opposite direction */
            unk_copy_copy.queryInterfaceTo(unk_copy.asOutParam());
            RTPrintf(" IUnknown(unk_copy_copy)=%p\n", (IUnknown *)unk_copy);
            Assert((IUnknown *)unk_copy == (IUnknown *)unk_copy_copy);

            /* query IUnknown again after releasing all previous IUnknown instances
             * but keeping IHost -- it should remain the same (Identity Rule) */
            IUnknown *oldUnk = unk;
            unk.setNull();
            unk_copy.setNull();
            unk_copy_copy.setNull();
            unk = host;
            RTPrintf(" IUnknown(host)=%p\n", (IUnknown *)unk);
            Assert(oldUnk == (IUnknown *)unk);
        }

//        RTPrintf("Will be now released (press Enter)...");
//        getchar();
    }
#endif

#if 0
    // the simplest COM API test
    ////////////////////////////////////////////////////////////////////////////
    {
        Bstr version;
        CHECK_ERROR_BREAK(virtualBox, COMGETTER(Version)(version.asOutParam()));
        RTPrintf("VirtualBox version = %ls\n", version.raw());
    }
#endif

#if 0
    // Array test
    ////////////////////////////////////////////////////////////////////////////
    {
        RTPrintf("Calling IVirtualBox::Machines...\n");

        com::SafeIfaceArray<IMachine> machines;
        CHECK_ERROR_BREAK(virtualBox,
                          COMGETTER(Machines)(ComSafeArrayAsOutParam(machines)));

        RTPrintf("%u machines registered (machines.isNull()=%d).\n",
                machines.size(), machines.isNull());

        for (size_t i = 0; i < machines.size(); ++ i)
        {
            Bstr name;
            CHECK_ERROR_BREAK(machines[i], COMGETTER(Name)(name.asOutParam()));
            RTPrintf("machines[%u]='%s'\n", i, Utf8Str(name).raw());
        }

#if 0
        {
            RTPrintf("Testing [out] arrays...\n");
            com::SafeGUIDArray uuids;
            CHECK_ERROR_BREAK(virtualBox,
                              COMGETTER(Uuids)(ComSafeArrayAsOutParam(uuids)));

            for (size_t i = 0; i < uuids.size(); ++ i)
                RTPrintf("uuids[%u]=%RTuuid\n", i, &uuids[i]);
        }

        {
            RTPrintf("Testing [in] arrays...\n");
            com::SafeGUIDArray uuids(5);
            for (size_t i = 0; i < uuids.size(); ++ i)
            {
                Guid id;
                id.create();
                uuids[i] = id;
                RTPrintf("uuids[%u]=%RTuuid\n", i, &uuids[i]);
            }

            CHECK_ERROR_BREAK(virtualBox,
                              SetUuids(ComSafeArrayAsInParam(uuids)));
        }
#endif

    }
#endif

#if 0
    // some outdated stuff
    ////////////////////////////////////////////////////////////////////////////

    RTPrintf("Getting IHost interface...\n");
    IHost *host;
    rc = virtualBox->GetHost(&host);
    if (SUCCEEDED(rc))
    {
        IHostDVDDriveCollection *dvdColl;
        rc = host->GetHostDVDDrives(&dvdColl);
        if (SUCCEEDED(rc))
        {
            IHostDVDDrive *dvdDrive = NULL;
            dvdColl->GetNextHostDVDDrive(dvdDrive, &dvdDrive);
            while (dvdDrive)
            {
                BSTR driveName;
                char *driveNameUtf8;
                dvdDrive->GetDriveName(&driveName);
                RTUtf16ToUtf8((PCRTUTF16)driveName, &driveNameUtf8);
                RTPrintf("Host DVD drive name: %s\n", driveNameUtf8);
                RTStrFree(driveNameUtf8);
                SysFreeString(driveName);
                IHostDVDDrive *dvdDriveTemp = dvdDrive;
                dvdColl->GetNextHostDVDDrive(dvdDriveTemp, &dvdDrive);
                dvdDriveTemp->Release();
            }
            dvdColl->Release();
        } else
        {
            RTPrintf("Could not get host DVD drive collection\n");
        }

        IHostFloppyDriveCollection *floppyColl;
        rc = host->GetHostFloppyDrives(&floppyColl);
        if (SUCCEEDED(rc))
        {
            IHostFloppyDrive *floppyDrive = NULL;
            floppyColl->GetNextHostFloppyDrive(floppyDrive, &floppyDrive);
            while (floppyDrive)
            {
                BSTR driveName;
                char *driveNameUtf8;
                floppyDrive->GetDriveName(&driveName);
                RTUtf16ToUtf8((PCRTUTF16)driveName, &driveNameUtf8);
                RTPrintf("Host floppy drive name: %s\n", driveNameUtf8);
                RTStrFree(driveNameUtf8);
                SysFreeString(driveName);
                IHostFloppyDrive *floppyDriveTemp = floppyDrive;
                floppyColl->GetNextHostFloppyDrive(floppyDriveTemp, &floppyDrive);
                floppyDriveTemp->Release();
            }
            floppyColl->Release();
        } else
        {
            RTPrintf("Could not get host floppy drive collection\n");
        }
        host->Release();
    } else
    {
        RTPrintf("Call failed\n");
    }
    RTPrintf("\n");
#endif

#if 0
    // IVirtualBoxErrorInfo test
    ////////////////////////////////////////////////////////////////////////////
    {
        // RPC calls

        // call a method that will definitely fail
        Guid uuid;
        ComPtr<IHardDisk> hardDisk;
        rc = virtualBox->GetHardDisk(uuid, hardDisk.asOutParam());
        RTPrintf("virtualBox->GetHardDisk(null-uuid)=%08X\n", rc);

//        {
//            com::ErrorInfo info(virtualBox);
//            PRINT_ERROR_INFO(info);
//        }

        // call a method that will definitely succeed
        Bstr version;
        rc = virtualBox->COMGETTER(Version)(version.asOutParam());
        RTPrintf("virtualBox->COMGETTER(Version)=%08X\n", rc);

        {
            com::ErrorInfo info(virtualBox);
            PRINT_ERROR_INFO(info);
        }

        // Local calls

        // call a method that will definitely fail
        ComPtr<IMachine> machine;
        rc = session->COMGETTER(Machine)(machine.asOutParam());
        RTPrintf("session->COMGETTER(Machine)=%08X\n", rc);

//        {
//            com::ErrorInfo info(virtualBox);
//            PRINT_ERROR_INFO(info);
//        }

        // call a method that will definitely succeed
        SessionState_T state;
        rc = session->COMGETTER(State)(&state);
        RTPrintf("session->COMGETTER(State)=%08X\n", rc);

        {
            com::ErrorInfo info(virtualBox);
            PRINT_ERROR_INFO(info);
        }
    }
#endif

#if 0
    // register the existing hard disk image
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IHardDisk> hd;
        Bstr src = L"E:\\develop\\innotek\\images\\NewHardDisk.vdi";
        RTPrintf("Opening the existing hard disk '%ls'...\n", src.raw());
        CHECK_ERROR_BREAK(virtualBox, OpenHardDisk(src, AccessMode_ReadWrite, hd.asOutParam()));
        RTPrintf("Enter to continue...\n");
        getchar();
        RTPrintf("Registering the existing hard disk '%ls'...\n", src.raw());
        CHECK_ERROR_BREAK(virtualBox, RegisterHardDisk(hd));
        RTPrintf("Enter to continue...\n");
        getchar();
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // find and unregister the existing hard disk image
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IVirtualDiskImage> vdi;
        Bstr src = L"CreatorTest.vdi";
        RTPrintf("Unregistering the hard disk '%ls'...\n", src.raw());
        CHECK_ERROR_BREAK(virtualBox, FindVirtualDiskImage(src, vdi.asOutParam()));
        ComPtr<IHardDisk> hd = vdi;
        Guid id;
        CHECK_ERROR_BREAK(hd, COMGETTER(Id)(id.asOutParam()));
        CHECK_ERROR_BREAK(virtualBox, UnregisterHardDisk(id, hd.asOutParam()));
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // clone the registered hard disk
    ///////////////////////////////////////////////////////////////////////////
    do
    {
#if defined RT_OS_LINUX
        Bstr src = L"/mnt/hugaida/common/develop/innotek/images/freedos-linux.vdi";
#else
        Bstr src = L"E:/develop/innotek/images/freedos.vdi";
#endif
        Bstr dst = L"./clone.vdi";
        RTPrintf("Cloning '%ls' to '%ls'...\n", src.raw(), dst.raw());
        ComPtr<IVirtualDiskImage> vdi;
        CHECK_ERROR_BREAK(virtualBox, FindVirtualDiskImage(src, vdi.asOutParam()));
        ComPtr<IHardDisk> hd = vdi;
        ComPtr<IProgress> progress;
        CHECK_ERROR_BREAK(hd, CloneToImage(dst, vdi.asOutParam(), progress.asOutParam()));
        RTPrintf("Waiting for completion...\n");
        CHECK_ERROR_BREAK(progress, WaitForCompletion(-1));
        ProgressErrorInfo ei(progress);
        if (FAILED(ei.getResultCode()))
        {
            PRINT_ERROR_INFO(ei);
        }
        else
        {
            vdi->COMGETTER(FilePath)(dst.asOutParam());
            RTPrintf("Actual clone path is '%ls'\n", dst.raw());
        }
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // find a registered hard disk by location and get properties
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IHardDisk> hd;
        static const wchar_t *Names[] =
        {
#ifndef RT_OS_LINUX
            L"freedos.vdi",
            L"MS-DOS.vmdk",
            L"iscsi",
            L"some/path/and/disk.vdi",
#else
            L"xp.vdi",
            L"Xp.vDI",
#endif
        };

        RTPrintf("\n");

        for (size_t i = 0; i < RT_ELEMENTS(Names); ++ i)
        {
            Bstr src = Names[i];
            RTPrintf("Searching for hard disk '%ls'...\n", src.raw());
            rc = virtualBox->FindHardDisk(src, hd.asOutParam());
            if (SUCCEEDED(rc))
            {
                Guid id;
                Bstr location;
                CHECK_ERROR_BREAK(hd, COMGETTER(Id)(id.asOutParam()));
                CHECK_ERROR_BREAK(hd, COMGETTER(Location)(location.asOutParam()));
                RTPrintf("Found, UUID={%RTuuid}, location='%ls'.\n",
                        id.raw(), location.raw());

                com::SafeArray<BSTR> names;
                com::SafeArray<BSTR> values;

                CHECK_ERROR_BREAK(hd, GetProperties(NULL,
                                                    ComSafeArrayAsOutParam(names),
                                                    ComSafeArrayAsOutParam(values)));

                RTPrintf("Properties:\n");
                for (size_t i = 0; i < names.size(); ++ i)
                    RTPrintf(" %ls = %ls\n", names[i], values[i]);

                if (names.size() == 0)
                    RTPrintf(" <none>\n");

#if 0
                Bstr name("TargetAddress");
                Bstr value = Utf8StrFmt("lalala (%llu)", RTTimeMilliTS());

                RTPrintf("Settings property %ls to %ls...\n", name.raw(), value.raw());
                CHECK_ERROR(hd, SetProperty(name, value));
#endif
            }
            else
            {
                com::ErrorInfo info(virtualBox);
                PRINT_ERROR_INFO(info);
            }
            RTPrintf("\n");
        }
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // access the machine in read-only mode
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IMachine> machine;
        Bstr name = argc > 1 ? argv[1] : "dos";
        RTPrintf("Getting a machine object named '%ls'...\n", name.raw());
        CHECK_ERROR_BREAK(virtualBox, FindMachine(name, machine.asOutParam()));
        RTPrintf("Accessing the machine in read-only mode:\n");
        readAndChangeMachineSettings(machine);
    }
    while (0);
    RTPrintf("\n");
#endif

#if 0
    // create a new machine (w/o registering it)
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IMachine> machine;
#if defined(RT_OS_LINUX)
        Bstr baseDir = L"/tmp/vbox";
#else
        Bstr baseDir = L"C:\\vbox";
#endif
        Bstr name = L"machina";

        RTPrintf("Creating a new machine object(base dir '%ls', name '%ls')...\n",
                baseDir.raw(), name.raw());
        CHECK_ERROR_BREAK(virtualBox, CreateMachine(name, L"", baseDir, L"",
                                                    false,
                                                    machine.asOutParam()));

        RTPrintf("Getting name...\n");
        CHECK_ERROR_BREAK(machine, COMGETTER(Name)(name.asOutParam()));
        RTPrintf("Name: {%ls}\n", name.raw());

        BOOL modified = FALSE;
        RTPrintf("Are any settings modified?...\n");
        CHECK_ERROR_BREAK(machine, COMGETTER(SettingsModified)(&modified));
        RTPrintf("%s\n", modified ? "yes" : "no");

        ASSERT_BREAK(modified == TRUE);

        name = L"Kakaya prekrasnaya virtual'naya mashina!";
        RTPrintf("Setting new name ({%ls})...\n", name.raw());
        CHECK_ERROR_BREAK(machine, COMSETTER(Name)(name));

        RTPrintf("Setting memory size to 111...\n");
        CHECK_ERROR_BREAK(machine, COMSETTER(MemorySize)(111));

        Bstr desc = L"This is an exemplary description.";
        RTPrintf("Setting description to \"%ls\"...\n", desc.raw());
        CHECK_ERROR_BREAK(machine, COMSETTER(Description)(desc));

        ComPtr<IGuestOSType> guestOSType;
        Bstr type = L"os2warp45";
        CHECK_ERROR_BREAK(virtualBox, GetGuestOSType(type, guestOSType.asOutParam()));

        RTPrintf("Saving new machine settings...\n");
        CHECK_ERROR_BREAK(machine, SaveSettings());

        RTPrintf("Accessing the newly created machine:\n");
        readAndChangeMachineSettings(machine);
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // enumerate host DVD drives
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IHost> host;
        CHECK_RC_BREAK(virtualBox->COMGETTER(Host)(host.asOutParam()));

        {
            ComPtr<IHostDVDDriveCollection> coll;
            CHECK_RC_BREAK(host->COMGETTER(DVDDrives)(coll.asOutParam()));
            ComPtr<IHostDVDDriveEnumerator> enumerator;
            CHECK_RC_BREAK(coll->Enumerate(enumerator.asOutParam()));
            BOOL hasmore;
            while (SUCCEEDED(enumerator->HasMore(&hasmore)) && hasmore)
            {
                ComPtr<IHostDVDDrive> drive;
                CHECK_RC_BREAK(enumerator->GetNext(drive.asOutParam()));
                Bstr name;
                CHECK_RC_BREAK(drive->COMGETTER(Name)(name.asOutParam()));
                RTPrintf("Host DVD drive: name={%ls}\n", name.raw());
            }
            CHECK_RC_BREAK(rc);

            ComPtr<IHostDVDDrive> drive;
            CHECK_ERROR(enumerator, GetNext(drive.asOutParam()));
            CHECK_ERROR(coll, GetItemAt(1000, drive.asOutParam()));
            CHECK_ERROR(coll, FindByName(Bstr("R:"), drive.asOutParam()));
            if (SUCCEEDED(rc))
            {
                Bstr name;
                CHECK_RC_BREAK(drive->COMGETTER(Name)(name.asOutParam()));
                RTPrintf("Found by name: name={%ls}\n", name.raw());
            }
        }
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // check for available hd backends
    ///////////////////////////////////////////////////////////////////////////
    {
        RTPrintf("Supported hard disk backends: --------------------------\n");
        ComPtr<ISystemProperties> systemProperties;
        CHECK_ERROR_BREAK(virtualBox,
                          COMGETTER(SystemProperties)(systemProperties.asOutParam()));
        com::SafeIfaceArray<IHardDiskFormat> hardDiskFormats;
        CHECK_ERROR_BREAK(systemProperties,
                          COMGETTER(HardDiskFormats)(ComSafeArrayAsOutParam(hardDiskFormats)));

        for (size_t i = 0; i < hardDiskFormats.size(); ++ i)
        {
            /* General information */
            Bstr id;
            CHECK_ERROR_BREAK(hardDiskFormats[i],
                              COMGETTER(Id)(id.asOutParam()));

            Bstr description;
            CHECK_ERROR_BREAK(hardDiskFormats[i],
                              COMGETTER(Id)(description.asOutParam()));

            ULONG caps;
            CHECK_ERROR_BREAK(hardDiskFormats[i],
                              COMGETTER(Capabilities)(&caps));

            RTPrintf("Backend %u: id='%ls' description='%ls' capabilities=%#06x extensions='",
                     i, id.raw(), description.raw(), caps);

            /* File extensions */
            com::SafeArray<BSTR> fileExtensions;
            CHECK_ERROR_BREAK(hardDiskFormats[i],
                              COMGETTER(FileExtensions)(ComSafeArrayAsOutParam(fileExtensions)));
            for (size_t a = 0; a < fileExtensions.size(); ++ a)
            {
                RTPrintf("%ls", Bstr(fileExtensions[a]).raw());
                if (a != fileExtensions.size()-1)
                    RTPrintf(",");
            }
            RTPrintf("'");

            /* Configuration keys */
            com::SafeArray<BSTR> propertyNames;
            com::SafeArray<BSTR> propertyDescriptions;
            com::SafeArray<ULONG> propertyTypes;
            com::SafeArray<ULONG> propertyFlags;
            com::SafeArray<BSTR> propertyDefaults;
            CHECK_ERROR_BREAK(hardDiskFormats[i],
                              DescribeProperties(ComSafeArrayAsOutParam(propertyNames),
                                                 ComSafeArrayAsOutParam(propertyDescriptions),
                                                 ComSafeArrayAsOutParam(propertyTypes),
                                                 ComSafeArrayAsOutParam(propertyFlags),
                                                 ComSafeArrayAsOutParam(propertyDefaults)));

            RTPrintf(" config=(");
            if (propertyNames.size() > 0)
            {
                for (size_t a = 0; a < propertyNames.size(); ++ a)
                {
                    RTPrintf("key='%ls' desc='%ls' type=", Bstr(propertyNames[a]).raw(), Bstr(propertyDescriptions[a]).raw());
                    switch (propertyTypes[a])
                    {
                        case DataType_Int32Type: RTPrintf("int"); break;
                        case DataType_Int8Type: RTPrintf("byte"); break;
                        case DataType_StringType: RTPrintf("string"); break;
                    }
                    RTPrintf(" flags=%#04x", propertyFlags[a]);
                    RTPrintf(" default='%ls'", Bstr(propertyDefaults[a]).raw());
                    if (a != propertyNames.size()-1)
                        RTPrintf(",");
                }
            }
            RTPrintf(")\n");
        }
        RTPrintf("-------------------------------------------------------\n");
    }
#endif

#if 0
    // enumerate hard disks & dvd images
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        {
            com::SafeIfaceArray<IHardDisk> disks;
            CHECK_ERROR_BREAK(virtualBox,
                              COMGETTER(HardDisks)(ComSafeArrayAsOutParam(disks)));

            RTPrintf("%u base hard disks registered (disks.isNull()=%d).\n",
                    disks.size(), disks.isNull());

            for (size_t i = 0; i < disks.size(); ++ i)
            {
                Bstr loc;
                CHECK_ERROR_BREAK(disks[i], COMGETTER(Location)(loc.asOutParam()));
                Guid id;
                CHECK_ERROR_BREAK(disks[i], COMGETTER(Id)(id.asOutParam()));
                MediaState_T state;
                CHECK_ERROR_BREAK(disks[i], COMGETTER(State)(&state));
                Bstr format;
                CHECK_ERROR_BREAK(disks[i], COMGETTER(Format)(format.asOutParam()));

                RTPrintf(" disks[%u]: '%ls'\n"
                        "  UUID:         {%RTuuid}\n"
                        "  State:        %s\n"
                        "  Format:       %ls\n",
                        i, loc.raw(), id.raw(),
                        state == MediaState_NotCreated ? "Not Created" :
                        state == MediaState_Created ? "Created" :
                        state == MediaState_Inaccessible ? "Inaccessible" :
                        state == MediaState_LockedRead ? "Locked Read" :
                        state == MediaState_LockedWrite ? "Locked Write" :
                        "???",
                        format.raw());

                if (state == MediaState_Inaccessible)
                {
                    Bstr error;
                    CHECK_ERROR_BREAK(disks[i],
                                      COMGETTER(LastAccessError)(error.asOutParam()));
                    RTPrintf("  Access Error: %ls\n", error.raw());
                }

                /* get usage */

                RTPrintf("  Used by VMs:\n");

                com::SafeGUIDArray ids;
                CHECK_ERROR_BREAK(disks[i],
                                  COMGETTER(MachineIds)(ComSafeArrayAsOutParam(ids)));
                if (ids.size() == 0)
                {
                    RTPrintf("   <not used>\n");
                }
                else
                {
                    for (size_t j = 0; j < ids.size(); ++ j)
                    {
                        RTPrintf("   {%RTuuid}\n", &ids[j]);
                    }
                }
            }
        }
        {
            com::SafeIfaceArray<IDVDImage> images;
            CHECK_ERROR_BREAK(virtualBox,
                              COMGETTER(DVDImages)(ComSafeArrayAsOutParam(images)));

            RTPrintf("%u DVD images registered (images.isNull()=%d).\n",
                    images.size(), images.isNull());

            for (size_t i = 0; i < images.size(); ++ i)
            {
                Bstr loc;
                CHECK_ERROR_BREAK(images[i], COMGETTER(Location)(loc.asOutParam()));
                Guid id;
                CHECK_ERROR_BREAK(images[i], COMGETTER(Id)(id.asOutParam()));
                MediaState_T state;
                CHECK_ERROR_BREAK(images[i], COMGETTER(State)(&state));

                RTPrintf(" images[%u]: '%ls'\n"
                        "  UUID:         {%RTuuid}\n"
                        "  State:        %s\n",
                        i, loc.raw(), id.raw(),
                        state == MediaState_NotCreated ? "Not Created" :
                        state == MediaState_Created ? "Created" :
                        state == MediaState_Inaccessible ? "Inaccessible" :
                        state == MediaState_LockedRead ? "Locked Read" :
                        state == MediaState_LockedWrite ? "Locked Write" :
                        "???");

                if (state == MediaState_Inaccessible)
                {
                    Bstr error;
                    CHECK_ERROR_BREAK(images[i],
                                      COMGETTER(LastAccessError)(error.asOutParam()));
                    RTPrintf("  Access Error: %ls\n", error.raw());
                }

                /* get usage */

                RTPrintf("  Used by VMs:\n");

                com::SafeGUIDArray ids;
                CHECK_ERROR_BREAK(images[i],
                                  COMGETTER(MachineIds)(ComSafeArrayAsOutParam(ids)));
                if (ids.size() == 0)
                {
                    RTPrintf("   <not used>\n");
                }
                else
                {
                    for (size_t j = 0; j < ids.size(); ++ j)
                    {
                        RTPrintf("   {%RTuuid}\n", &ids[j]);
                    }
                }
            }
        }
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // open a (direct) session
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IMachine> machine;
        Bstr name = argc > 1 ? argv[1] : "dos";
        RTPrintf("Getting a machine object named '%ls'...\n", name.raw());
        CHECK_ERROR_BREAK(virtualBox, FindMachine(name, machine.asOutParam()));
        Guid guid;
        CHECK_RC_BREAK(machine->COMGETTER(Id)(guid.asOutParam()));
        RTPrintf("Opening a session for this machine...\n");
        CHECK_RC_BREAK(virtualBox->OpenSession(session, guid));
#if 1
        ComPtr<IMachine> sessionMachine;
        RTPrintf("Getting machine session object...\n");
        CHECK_RC_BREAK(session->COMGETTER(Machine)(sessionMachine.asOutParam()));
        RTPrintf("Accessing the machine within the session:\n");
        readAndChangeMachineSettings(sessionMachine, machine);
#if 0
        RTPrintf("\n");
        RTPrintf("Enabling the VRDE server (must succeed even if the VM is saved):\n");
        ComPtr<IVRDEServer> vrdeServer;
        CHECK_ERROR_BREAK(sessionMachine, COMGETTER(VRDEServer)(vrdeServer.asOutParam()));
        if (FAILED(vrdeServer->COMSETTER(Enabled)(TRUE)))
        {
            PRINT_ERROR_INFO(com::ErrorInfo(vrdeServer));
        }
        else
        {
            BOOL enabled = FALSE;
            CHECK_ERROR_BREAK(vrdeServer, COMGETTER(Enabled)(&enabled));
            RTPrintf("VRDE server is %s\n", enabled ? "enabled" : "disabled");
        }
#endif
#endif
#if 0
        ComPtr<IConsole> console;
        RTPrintf("Getting the console object...\n");
        CHECK_RC_BREAK(session->COMGETTER(Console)(console.asOutParam()));
        RTPrintf("Discarding the current machine state...\n");
        ComPtr<IProgress> progress;
        CHECK_ERROR_BREAK(console, DiscardCurrentState(progress.asOutParam()));
        RTPrintf("Waiting for completion...\n");
        CHECK_ERROR_BREAK(progress, WaitForCompletion(-1));
        ProgressErrorInfo ei(progress);
        if (FAILED(ei.getResultCode()))
        {
            PRINT_ERROR_INFO(ei);

            ComPtr<IUnknown> initiator;
            CHECK_ERROR_BREAK(progress, COMGETTER(Initiator)(initiator.asOutParam()));

            RTPrintf("initiator(unk) = %p\n", (IUnknown *)initiator);
            RTPrintf("console(unk) = %p\n", (IUnknown *)ComPtr<IUnknown>((IConsole *)console));
            RTPrintf("console = %p\n", (IConsole *)console);
        }
#endif
        RTPrintf("Press enter to close session...");
        getchar();
        session->Close();
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // open a remote session
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IMachine> machine;
        Bstr name = L"dos";
        RTPrintf("Getting a machine object named '%ls'...\n", name.raw());
        CHECK_RC_BREAK(virtualBox->FindMachine(name, machine.asOutParam()));
        Guid guid;
        CHECK_RC_BREAK(machine->COMGETTER(Id)(guid.asOutParam()));
        RTPrintf("Opening a remote session for this machine...\n");
        ComPtr<IProgress> progress;
        CHECK_RC_BREAK(virtualBox->OpenRemoteSession(session, guid, Bstr("gui"),
                                                       NULL, progress.asOutParam()));
        RTPrintf("Waiting for the session to open...\n");
        CHECK_RC_BREAK(progress->WaitForCompletion(-1));
        ComPtr<IMachine> sessionMachine;
        RTPrintf("Getting machine session object...\n");
        CHECK_RC_BREAK(session->COMGETTER(Machine)(sessionMachine.asOutParam()));
        ComPtr<IConsole> console;
        RTPrintf("Getting console object...\n");
        CHECK_RC_BREAK(session->COMGETTER(Console)(console.asOutParam()));
        RTPrintf("Press enter to pause the VM execution in the remote session...");
        getchar();
        CHECK_RC(console->Pause());
        RTPrintf("Press enter to close this session...");
        getchar();
        session->Close();
    }
    while (FALSE);
    RTPrintf("\n");
#endif

#if 0
    // open an existing remote session
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        ComPtr<IMachine> machine;
        Bstr name = "dos";
        RTPrintf("Getting a machine object named '%ls'...\n", name.raw());
        CHECK_RC_BREAK(virtualBox->FindMachine(name, machine.asOutParam()));
        Guid guid;
        CHECK_RC_BREAK(machine->COMGETTER(Id)(guid.asOutParam()));
        RTPrintf("Opening an existing remote session for this machine...\n");
        CHECK_RC_BREAK(virtualBox->OpenExistingSession(session, guid));
        ComPtr<IMachine> sessionMachine;
        RTPrintf("Getting machine session object...\n");
        CHECK_RC_BREAK(session->COMGETTER(Machine)(sessionMachine.asOutParam()));
#if 0
        ComPtr<IConsole> console;
        RTPrintf("Getting console object...\n");
        CHECK_RC_BREAK(session->COMGETTER(Console)(console.asOutParam()));
        RTPrintf("Press enter to pause the VM execution in the remote session...");
        getchar();
        CHECK_RC(console->Pause());
        RTPrintf("Press enter to close this session...");
        getchar();
#endif
        session->Close();
    }
    while (FALSE);
    RTPrintf("\n");
#endif

    do {
        PlatformArchitecture_T platformArch = PlatformArchitecture_x86;
        ComPtr<IPlatformProperties> platformProperties;
        CHECK_ERROR_BREAK(virtualBox, GetPlatformProperties(platformArch, platformProperties.asOutParam()));
        ULONG uMinMB, uMaxMB, uStrideMB;
        CHECK_ERROR_BREAK(platformProperties, GetSupportedVRAMRange(GraphicsControllerType_VBoxVGA, TRUE /* fAccelerate3DEnabled */, &uMinMB, &uMaxMB, &uStrideMB));
        ASSERT_BREAK(uMinMB && RT_IS_POWER_OF_TWO(uMinMB));
        ASSERT_BREAK(uMaxMB && RT_IS_POWER_OF_TWO(uMaxMB));
        ASSERT_BREAK(uStrideMB && RT_IS_POWER_OF_TWO(uStrideMB));
    } while (0);

#if 1
    do {
        // Get host
        ComPtr<IHost> host;
        CHECK_ERROR_BREAK(virtualBox, COMGETTER(Host)(host.asOutParam()));

        ULONG uMemSize, uMemAvail;
        CHECK_ERROR_BREAK(host, COMGETTER(MemorySize)(&uMemSize));
        RTPrintf("Total memory (MB): %u\n", uMemSize);
        CHECK_ERROR_BREAK(host, COMGETTER(MemoryAvailable)(&uMemAvail));
        RTPrintf("Free memory (MB): %u\n", uMemAvail);
    } while (0);
#endif

#if 0
    do {
        // Get host
        ComPtr<IHost> host;
        CHECK_ERROR_BREAK(virtualBox, COMGETTER(Host)(host.asOutParam()));

        com::SafeIfaceArray<IHostNetworkInterface> hostNetworkInterfaces;
        CHECK_ERROR_BREAK(host,
                          COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(hostNetworkInterfaces)));
        if (hostNetworkInterfaces.size() > 0)
        {
            ComPtr<IHostNetworkInterface> networkInterface = hostNetworkInterfaces[0];
            Bstr interfaceName;
            networkInterface->COMGETTER(Name)(interfaceName.asOutParam());
            RTPrintf("Found %d network interfaces, testing with %ls...\n", hostNetworkInterfaces.size(), interfaceName.raw());
            Bstr interfaceGuid;
            networkInterface->COMGETTER(Id)(interfaceGuid.asOutParam());
            // Find the interface by its name
            networkInterface.setNull();
            CHECK_ERROR_BREAK(host,
                              FindHostNetworkInterfaceByName(interfaceName.raw(), networkInterface.asOutParam()));
            Bstr interfaceGuid2;
            networkInterface->COMGETTER(Id)(interfaceGuid2.asOutParam());
            if (interfaceGuid2 != interfaceGuid)
                RTPrintf("Failed to retrieve an interface by name %ls.\n", interfaceName.raw());
            // Find the interface by its guid
            networkInterface.setNull();
            CHECK_ERROR_BREAK(host,
                              FindHostNetworkInterfaceById(interfaceGuid.raw(), networkInterface.asOutParam()));
            Bstr interfaceName2;
            networkInterface->COMGETTER(Name)(interfaceName2.asOutParam());
            if (interfaceName != interfaceName2)
                RTPrintf("Failed to retrieve an interface by GUID %ls.\n", interfaceGuid.raw());
        }
        else
        {
            RTPrintf("No network interfaces found!\n");
        }
    } while (0);
#endif

#if 0
    // DNS & Co.
    ///////////////////////////////////////////////////////////////////////////
    /* XXX: Note it's endless loop */
    do
    {
        ComPtr<IHost> host;
        CHECK_ERROR_BREAK(virtualBox, COMGETTER(Host)(host.asOutParam()));

        {
            Bstr domainName;
            CHECK_ERROR_BREAK(host,COMGETTER(DomainName)(domainName.asOutParam()));
            RTPrintf("Domain name: %ls\n", domainName.raw());
        }

        com::SafeArray<BSTR> strs;
        CHECK_ERROR_BREAK(host, COMGETTER(NameServers)(ComSafeArrayAsOutParam(strs)));

        unsigned int i;
        for (i = 0; i < strs.size(); ++i)
            RTPrintf("Name server[%d]:%s\n", i, com::Utf8Str(strs[i]).c_str());

        RTThreadSleep(1000);
    }
    while (1);
    RTPrintf("\n");
#endif


#if 0 && defined(VBOX_WITH_RESOURCE_USAGE_API)
    do {
        // Get collector
        ComPtr<IPerformanceCollector> collector;
        CHECK_ERROR_BREAK(virtualBox,
                          COMGETTER(PerformanceCollector)(collector.asOutParam()));


        // Fill base metrics array
        Bstr baseMetricNames[] = { L"Net/eth0/Load" };
        com::SafeArray<BSTR> baseMetrics(1);
        baseMetricNames[0].cloneTo(&baseMetrics[0]);

        // Get host
        ComPtr<IHost> host;
        CHECK_ERROR_BREAK(virtualBox, COMGETTER(Host)(host.asOutParam()));

        // Get host network interfaces
        // com::SafeIfaceArray<IHostNetworkInterface> hostNetworkInterfaces;
        // CHECK_ERROR_BREAK(host,
        //                   COMGETTER(NetworkInterfaces)(ComSafeArrayAsOutParam(hostNetworkInterfaces)));

        // Setup base metrics
        // Note that one needs to set up metrics after a session is open for a machine.
        com::SafeIfaceArray<IPerformanceMetric> affectedMetrics;
        com::SafeIfaceArray<IUnknown> objects(1);
        host.queryInterfaceTo(&objects[0]);
        CHECK_ERROR_BREAK(collector, SetupMetrics(ComSafeArrayAsInParam(baseMetrics),
                                                   ComSafeArrayAsInParam(objects), 1u, 10u,
                                                   ComSafeArrayAsOutParam(affectedMetrics)));
        listAffectedMetrics(virtualBox,
                            ComSafeArrayAsInParam(affectedMetrics));
        affectedMetrics.setNull();

        RTPrintf("Sleeping for 5 seconds...\n");
        RTThreadSleep(5000); // Sleep for 5 seconds

        RTPrintf("\nMetrics collected: --------------------\n");
        queryMetrics(virtualBox, collector, ComSafeArrayAsInParam(objects));
    } while (false);
#endif /* VBOX_WITH_RESOURCE_USAGE_API */
#if 0 && defined(VBOX_WITH_RESOURCE_USAGE_API)
    do {
        // Get collector
        ComPtr<IPerformanceCollector> collector;
        CHECK_ERROR_BREAK(virtualBox,
                          COMGETTER(PerformanceCollector)(collector.asOutParam()));


        // Fill base metrics array
        //Bstr baseMetricNames[] = { L"CPU/Load,RAM/Usage" };
        Bstr baseMetricNames[] = { L"RAM/VMM" };
        com::SafeArray<BSTR> baseMetrics(1);
        baseMetricNames[0].cloneTo(&baseMetrics[0]);

        // Get host
        ComPtr<IHost> host;
        CHECK_ERROR_BREAK(virtualBox, COMGETTER(Host)(host.asOutParam()));

        // Get machine
        ComPtr<IMachine> machine;
        Bstr name = argc > 1 ? argv[1] : "dsl";
        Bstr sessionType = argc > 2 ? argv[2] : "headless";
        RTPrintf("Getting a machine object named '%ls'...\n", name.raw());
        CHECK_ERROR_BREAK(virtualBox, FindMachine(name.raw(), machine.asOutParam()));

        // Open session
        ComPtr<IProgress> progress;
        RTPrintf("Launching VM process...\n");
        CHECK_ERROR_BREAK(machine, LaunchVMProcess(session, sessionType.raw(),
                                                   ComSafeArrayNullInParam(), progress.asOutParam()));
        RTPrintf("Waiting for the VM to power on...\n");
        CHECK_ERROR_BREAK(progress, WaitForCompletion(-1));

        // ComPtr<IMachine> sessionMachine;
        // RTPrintf("Getting machine session object...\n");
        // CHECK_ERROR_BREAK(session, COMGETTER(Machine)(sessionMachine.asOutParam()));

        // Setup base metrics
        // Note that one needs to set up metrics after a session is open for a machine.
        com::SafeIfaceArray<IPerformanceMetric> affectedMetrics;
        com::SafeIfaceArray<IUnknown> objects(1);
        host.queryInterfaceTo(&objects[0]);
        //machine.queryInterfaceTo(&objects[1]);
        CHECK_ERROR_BREAK(collector, SetupMetrics(ComSafeArrayAsInParam(baseMetrics),
                                                   ComSafeArrayAsInParam(objects), 1u, 10u,
                                                   ComSafeArrayAsOutParam(affectedMetrics)));
        listAffectedMetrics(virtualBox,
                            ComSafeArrayAsInParam(affectedMetrics));
        affectedMetrics.setNull();

        // Get console
        ComPtr<IConsole> console;
        RTPrintf("Getting console object...\n");
        CHECK_ERROR_BREAK(session, COMGETTER(Console)(console.asOutParam()));

        RTThreadSleep(5000); // Sleep for 5 seconds

        RTPrintf("\nMetrics collected with VM running: --------------------\n");
        queryMetrics(virtualBox, collector, ComSafeArrayAsInParam(objects));

        // Pause
        //RTPrintf("Press enter to pause the VM execution in the remote session...");
        //getchar();
        CHECK_ERROR_BREAK(console, Pause());

        RTThreadSleep(5000); // Sleep for 5 seconds

        RTPrintf("\nMetrics collected with VM paused: ---------------------\n");
        queryMetrics(virtualBox, collector, ComSafeArrayAsInParam(objects));

        RTPrintf("\nDrop collected metrics: ----------------------------------------\n");
        CHECK_ERROR_BREAK(collector,
            SetupMetrics(ComSafeArrayAsInParam(baseMetrics),
                         ComSafeArrayAsInParam(objects),
                         1u, 5u, ComSafeArrayAsOutParam(affectedMetrics)));
        listAffectedMetrics(virtualBox,
                            ComSafeArrayAsInParam(affectedMetrics));
        affectedMetrics.setNull();
        queryMetrics(virtualBox, collector, ComSafeArrayAsInParam(objects));

        com::SafeIfaceArray<IUnknown> vmObject(1);
        machine.queryInterfaceTo(&vmObject[0]);

        RTPrintf("\nDisable collection of VM metrics: ------------------------------\n");
        CHECK_ERROR_BREAK(collector,
            DisableMetrics(ComSafeArrayAsInParam(baseMetrics),
                           ComSafeArrayAsInParam(vmObject),
                           ComSafeArrayAsOutParam(affectedMetrics)));
        listAffectedMetrics(virtualBox,
                            ComSafeArrayAsInParam(affectedMetrics));
        affectedMetrics.setNull();
        RTThreadSleep(5000); // Sleep for 5 seconds
        queryMetrics(virtualBox, collector, ComSafeArrayAsInParam(objects));

        RTPrintf("\nRe-enable collection of all metrics: ---------------------------\n");
        CHECK_ERROR_BREAK(collector,
            EnableMetrics(ComSafeArrayAsInParam(baseMetrics),
                          ComSafeArrayAsInParam(objects),
                          ComSafeArrayAsOutParam(affectedMetrics)));
        listAffectedMetrics(virtualBox,
                            ComSafeArrayAsInParam(affectedMetrics));
        affectedMetrics.setNull();
        RTThreadSleep(5000); // Sleep for 5 seconds
        queryMetrics(virtualBox, collector, ComSafeArrayAsInParam(objects));

        // Power off
        RTPrintf("Press enter to power off VM...");
        getchar();
        CHECK_ERROR_BREAK(console, PowerDown(progress.asOutParam()));
        RTPrintf("Waiting for the VM to power down...\n");
        CHECK_ERROR_BREAK(progress, WaitForCompletion(-1));
        RTPrintf("Press enter to close this session...");
        getchar();
        session->UnlockMachine();
    } while (false);
#endif /* VBOX_WITH_RESOURCE_USAGE_API */
#if 0
    // check of OVF appliance handling
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        Bstr ovf = argc > 1 ? argv[1] : "someOVF.ovf";
        RTPrintf("Try to open %ls ...\n", ovf.raw());

        ComPtr<IAppliance> appliance;
        CHECK_ERROR_BREAK(virtualBox,
                          CreateAppliance(appliance.asOutParam()));
        CHECK_ERROR_BREAK(appliance, Read(ovf));
        Bstr path;
        CHECK_ERROR_BREAK(appliance, COMGETTER(Path)(path.asOutParam()));
        RTPrintf("Successfully opened %ls.\n", path.raw());
        CHECK_ERROR_BREAK(appliance, Interpret());
        RTPrintf("Successfully interpreted %ls.\n", path.raw());
        RTPrintf("Appliance:\n");
        // Fetch all disks
        com::SafeArray<BSTR> retDisks;
        CHECK_ERROR_BREAK(appliance,
                          COMGETTER(Disks)(ComSafeArrayAsOutParam(retDisks)));
        if (retDisks.size() > 0)
        {
            RTPrintf("Disks:");
            for (unsigned i = 0; i < retDisks.size(); i++)
                RTPrintf(" %ls", Bstr(retDisks[i]).raw());
            RTPrintf("\n");
        }
        /* Fetch all virtual system descriptions */
        com::SafeIfaceArray<IVirtualSystemDescription> retVSD;
        CHECK_ERROR_BREAK(appliance,
                          COMGETTER(VirtualSystemDescriptions)(ComSafeArrayAsOutParam(retVSD)));
        if (retVSD.size() > 0)
        {
            for (unsigned i = 0; i < retVSD.size(); ++i)
            {
                com::SafeArray<VirtualSystemDescriptionType_T> retTypes;
                com::SafeArray<BSTR> retRefValues;
                com::SafeArray<BSTR> retOrigValues;
                com::SafeArray<BSTR> retAutoValues;
                com::SafeArray<BSTR> retConfiguration;
                CHECK_ERROR_BREAK(retVSD[i],
                                  GetDescription(ComSafeArrayAsOutParam(retTypes),
                                                 ComSafeArrayAsOutParam(retRefValues),
                                                 ComSafeArrayAsOutParam(retOrigValues),
                                                 ComSafeArrayAsOutParam(retAutoValues),
                                                 ComSafeArrayAsOutParam(retConfiguration)));

                RTPrintf("VirtualSystemDescription:\n");
                for (unsigned a = 0; a < retTypes.size(); ++a)
                {
                    RTPrintf(" %d %ls %ls %ls\n",
                            retTypes[a],
                            Bstr(retOrigValues[a]).raw(),
                            Bstr(retAutoValues[a]).raw(),
                            Bstr(retConfiguration[a]).raw());
                }
                /* Show warnings from interpret */
                com::SafeArray<BSTR> retWarnings;
                CHECK_ERROR_BREAK(retVSD[i],
                                  GetWarnings(ComSafeArrayAsOutParam(retWarnings)));
                if (retWarnings.size() > 0)
                {
                    RTPrintf("The following warnings occurs on interpret:\n");
                    for (unsigned r = 0; r < retWarnings.size(); ++r)
                        RTPrintf("%ls\n", Bstr(retWarnings[r]).raw());
                    RTPrintf("\n");
                }
            }
            RTPrintf("\n");
        }
        RTPrintf("Try to import the appliance ...\n");
        ComPtr<IProgress> progress;
        CHECK_ERROR_BREAK(appliance,
                          ImportMachines(progress.asOutParam()));
        CHECK_ERROR(progress, WaitForCompletion(-1));
        if (SUCCEEDED(rc))
        {
            /* Check if the import was successfully */
            progress->COMGETTER(ResultCode)(&rc);
            if (FAILED(rc))
            {
                com::ProgressErrorInfo info(progress);
                if (info.isBasicAvailable())
                    RTPrintf("Error: failed to import appliance. Error message: %ls\n", info.getText().raw());
                else
                    RTPrintf("Error: failed to import appliance. No error message available!\n");
            }
            else
                RTPrintf("Successfully imported the appliance.\n");
        }

    }
    while (FALSE);
    RTPrintf("\n");
#endif
#if 0
    // check of network bandwidth control
    ///////////////////////////////////////////////////////////////////////////
    do
    {
        Bstr name = argc > 1 ? argv[1] : "ubuntu";
        Bstr sessionType = argc > 2 ? argv[2] : "headless";
        Bstr grpName = "tstAPI";
        {
            // Get machine
            ComPtr<IMachine> machine;
            ComPtr<IBandwidthControl> bwCtrl;
            ComPtr<IBandwidthGroup> bwGroup;
            ComPtr<INetworkAdapter> nic;
            RTPrintf("Getting a machine object named '%ls'...\n", name.raw());
            CHECK_ERROR_BREAK(virtualBox, FindMachine(name.raw(), machine.asOutParam()));
            /* open a session for the VM (new or shared) */
            CHECK_ERROR_BREAK(machine, LockMachine(session, LockType_Shared));
            SessionType_T st;
            CHECK_ERROR_BREAK(session, COMGETTER(Type)(&st));
            bool fRunTime = (st == SessionType_Shared);
            if (fRunTime)
            {
                RTPrintf("Machine %ls must not be running!\n");
                break;
            }
            /* get the mutable session machine */
            session->COMGETTER(Machine)(machine.asOutParam());
            CHECK_ERROR_BREAK(machine, COMGETTER(BandwidthControl)(bwCtrl.asOutParam()));

            RTPrintf("Creating bandwidth group named '%ls'...\n", grpName.raw());
            CHECK_ERROR_BREAK(bwCtrl, CreateBandwidthGroup(grpName.raw(), BandwidthGroupType_Network, 123));


            CHECK_ERROR_BREAK(bwCtrl, GetBandwidthGroup(grpName.raw(), bwGroup.asOutParam()));
            CHECK_ERROR_BREAK(machine, GetNetworkAdapter(0, nic.asOutParam()));
            RTPrintf("Assigning the group to the first network adapter...\n");
            CHECK_ERROR_BREAK(nic, COMSETTER(BandwidthGroup)(bwGroup));
            CHECK_ERROR_BREAK(machine, SaveSettings());
            RTPrintf("Press enter to close this session...");
            getchar();
            session->UnlockMachine();
        }
        {
            // Get machine
            ComPtr<IMachine> machine;
            ComPtr<IBandwidthControl> bwCtrl;
            ComPtr<IBandwidthGroup> bwGroup;
            Bstr grpNameReadFromNic;
            ComPtr<INetworkAdapter> nic;
            RTPrintf("Getting a machine object named '%ls'...\n", name.raw());
            CHECK_ERROR_BREAK(virtualBox, FindMachine(name.raw(), machine.asOutParam()));
            /* open a session for the VM (new or shared) */
            CHECK_ERROR_BREAK(machine, LockMachine(session, LockType_Shared));
            /* get the mutable session machine */
            session->COMGETTER(Machine)(machine.asOutParam());
            CHECK_ERROR_BREAK(machine, COMGETTER(BandwidthControl)(bwCtrl.asOutParam()));
            CHECK_ERROR_BREAK(machine, GetNetworkAdapter(0, nic.asOutParam()));

            RTPrintf("Reading the group back from the first network adapter...\n");
            CHECK_ERROR_BREAK(nic, COMGETTER(BandwidthGroup)(bwGroup.asOutParam()));
            if (bwGroup.isNull())
                RTPrintf("Error: Bandwidth group is null at the first network adapter!\n");
            else
            {
                CHECK_ERROR_BREAK(bwGroup, COMGETTER(Name)(grpNameReadFromNic.asOutParam()));
                if (grpName != grpNameReadFromNic)
                    RTPrintf("Error: Bandwidth group names do not match (%ls != %ls)!\n", grpName.raw(), grpNameReadFromNic.raw());
                else
                    RTPrintf("Successfully retrieved bandwidth group attribute from NIC (name=%ls)\n", grpNameReadFromNic.raw());
                ComPtr<IBandwidthGroup> bwGroupEmpty;
                RTPrintf("Assigning an empty group to the first network adapter...\n");
                CHECK_ERROR_BREAK(nic, COMSETTER(BandwidthGroup)(bwGroupEmpty));
            }
            RTPrintf("Removing bandwidth group named '%ls'...\n", grpName.raw());
            CHECK_ERROR_BREAK(bwCtrl, DeleteBandwidthGroup(grpName.raw()));
            CHECK_ERROR_BREAK(machine, SaveSettings());
            RTPrintf("Press enter to close this session...");
            getchar();
            session->UnlockMachine();
        }
    } while (FALSE);
    RTPrintf("\n");
#endif

    RTPrintf("Press enter to release Session and VirtualBox instances...");
    getchar();

    // end "all-stuff" scope
    ////////////////////////////////////////////////////////////////////////////
    }
    while (0);

    RTPrintf("Press enter to shutdown COM...");
    getchar();

    com::Shutdown();

    RTPrintf("tstAPI FINISHED.\n");

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

#ifdef VBOX_WITH_RESOURCE_USAGE_API

static void queryMetrics(ComPtr<IVirtualBox> aVirtualBox,
                         ComPtr<IPerformanceCollector> collector,
                         ComSafeArrayIn(IUnknown *, objects))
{
    HRESULT rc;

    //Bstr metricNames[] = { L"CPU/Load/User:avg,CPU/Load/System:avg,CPU/Load/Idle:avg,RAM/Usage/Total,RAM/Usage/Used:avg" };
    Bstr metricNames[] = { L"*" };
    com::SafeArray<BSTR> metrics(1);
    metricNames[0].cloneTo(&metrics[0]);
    com::SafeArray<BSTR>          retNames;
    com::SafeIfaceArray<IUnknown> retObjects;
    com::SafeArray<BSTR>          retUnits;
    com::SafeArray<ULONG>         retScales;
    com::SafeArray<ULONG>         retSequenceNumbers;
    com::SafeArray<ULONG>         retIndices;
    com::SafeArray<ULONG>         retLengths;
    com::SafeArray<LONG>          retData;
    CHECK_ERROR(collector, QueryMetricsData(ComSafeArrayAsInParam(metrics),
                                            ComSafeArrayInArg(objects),
                                            ComSafeArrayAsOutParam(retNames),
                                            ComSafeArrayAsOutParam(retObjects),
                                            ComSafeArrayAsOutParam(retUnits),
                                            ComSafeArrayAsOutParam(retScales),
                                            ComSafeArrayAsOutParam(retSequenceNumbers),
                                            ComSafeArrayAsOutParam(retIndices),
                                            ComSafeArrayAsOutParam(retLengths),
                                            ComSafeArrayAsOutParam(retData)));
    RTPrintf("Object     Metric               Values\n"
             "---------- -------------------- --------------------------------------------\n");
    for (unsigned i = 0; i < retNames.size(); i++)
    {
        Bstr metricUnit(retUnits[i]);
        Bstr metricName(retNames[i]);
        RTPrintf("%-10ls %-20ls ", getObjectName(aVirtualBox, retObjects[i]).raw(), metricName.raw());
        const char *separator = "";
        for (unsigned j = 0; j < retLengths[i]; j++)
        {
            if (retScales[i] == 1)
                RTPrintf("%s%d %ls", separator, retData[retIndices[i] + j], metricUnit.raw());
            else
                RTPrintf("%s%d.%02d%ls", separator, retData[retIndices[i] + j] / retScales[i],
                         (retData[retIndices[i] + j] * 100 / retScales[i]) % 100, metricUnit.raw());
            separator = ", ";
        }
        RTPrintf("\n");
    }
}

static Bstr getObjectName(ComPtr<IVirtualBox> aVirtualBox,
                                  ComPtr<IUnknown> aObject)
{
    HRESULT rc;

    ComPtr<IHost> host = aObject;
    if (!host.isNull())
        return Bstr("host");

    ComPtr<IMachine> machine = aObject;
    if (!machine.isNull())
    {
        Bstr name;
        CHECK_ERROR(machine, COMGETTER(Name)(name.asOutParam()));
        if (SUCCEEDED(rc))
            return name;
    }
    return Bstr("unknown");
}

static void listAffectedMetrics(ComPtr<IVirtualBox> aVirtualBox,
                                ComSafeArrayIn(IPerformanceMetric*, aMetrics))
{
    HRESULT rc;
    com::SafeIfaceArray<IPerformanceMetric> metrics(ComSafeArrayInArg(aMetrics));
    if (metrics.size())
    {
        ComPtr<IUnknown> object;
        Bstr metricName;
        RTPrintf("The following metrics were modified:\n\n"
                 "Object     Metric\n"
                 "---------- --------------------\n");
        for (size_t i = 0; i < metrics.size(); i++)
        {
            CHECK_ERROR(metrics[i], COMGETTER(Object)(object.asOutParam()));
            CHECK_ERROR(metrics[i], COMGETTER(MetricName)(metricName.asOutParam()));
            RTPrintf("%-10ls %-20ls\n",
                getObjectName(aVirtualBox, object).raw(), metricName.raw());
        }
        RTPrintf("\n");
    }
    else
    {
        RTPrintf("No metrics match the specified filter!\n");
    }
}

#endif /* VBOX_WITH_RESOURCE_USAGE_API */
/* vim: set shiftwidth=4 tabstop=4 expandtab: */
