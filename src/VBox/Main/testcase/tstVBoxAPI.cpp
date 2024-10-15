/* $Id: tstVBoxAPI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstVBoxAPI - Checks VirtualBox API.
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
#include <VBox/com/array.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/sup.h>

#include <iprt/test.h>
#include <iprt/time.h>

using namespace com;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;
static Bstr   tstMachineName = "tstVBoxAPI test VM";


/** Worker for TST_COM_EXPR(). */
static HRESULT tstComExpr(HRESULT hrc, const char *pszOperation, int iLine)
{
    if (FAILED(hrc))
        RTTestFailed(g_hTest, "%s failed on line %u with hrc=%Rhrc", pszOperation, iLine, hrc);
    return hrc;
}

/** Macro that executes the given expression and report any failure.
 *  The expression must return a HRESULT. */
#define TST_COM_EXPR(expr) tstComExpr(expr, #expr, __LINE__)


static void tstApiIVirtualBox(IVirtualBox *pVBox)
{
    HRESULT hrc;
    Bstr bstrTmp;
    ULONG ulTmp;

    RTTestSub(g_hTest, "IVirtualBox::version");
    CHECK_ERROR(pVBox, COMGETTER(Version)(bstrTmp.asOutParam()));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::version");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::version failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::versionNormalized");
    CHECK_ERROR(pVBox, COMGETTER(VersionNormalized)(bstrTmp.asOutParam()));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::versionNormalized");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::versionNormalized failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::revision");
    CHECK_ERROR(pVBox, COMGETTER(Revision)(&ulTmp));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::revision");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::revision failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::packageType");
    CHECK_ERROR(pVBox, COMGETTER(PackageType)(bstrTmp.asOutParam()));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::packageType");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::packageType failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::APIVersion");
    CHECK_ERROR(pVBox, COMGETTER(APIVersion)(bstrTmp.asOutParam()));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::APIVersion");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::APIVersion failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::homeFolder");
    CHECK_ERROR(pVBox, COMGETTER(HomeFolder)(bstrTmp.asOutParam()));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::homeFolder");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::homeFolder failed", __LINE__);

    RTTestSub(g_hTest, "IVirtualBox::settingsFilePath");
    CHECK_ERROR(pVBox, COMGETTER(SettingsFilePath)(bstrTmp.asOutParam()));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::settingsFilePath");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::settingsFilePath failed", __LINE__);

    com::SafeIfaceArray<IGuestOSType> guestOSTypes;
    RTTestSub(g_hTest, "IVirtualBox::guestOSTypes");
    CHECK_ERROR(pVBox, COMGETTER(GuestOSTypes)(ComSafeArrayAsOutParam(guestOSTypes)));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::guestOSTypes");
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::guestOSTypes failed", __LINE__);

    /* For now we only test same-same architectures (host == guest). */
    PlatformArchitecture_T enmPlatformArch;
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    enmPlatformArch = PlatformArchitecture_x86;
#elif defined(RT_ARCH_ARM32) || defined(RT_ARCH_ARM64)
    enmPlatformArch = PlatformArchitecture_ARM;
#else
# error "Port me!"
#endif

    /*
     * Host
     */
    ComPtr<IHost> host;
    RTTestSub(g_hTest, "IVirtualBox::host");
    CHECK_ERROR(pVBox, COMGETTER(Host)(host.asOutParam()));
    if (SUCCEEDED(hrc))
    {
        RTTestPassed(g_hTest, "IVirtualBox::host");
        CHECK_ERROR(host, COMGETTER(Architecture)(&enmPlatformArch));

        switch (enmPlatformArch)
        {
            case PlatformArchitecture_x86:
            {
                ComPtr<IHostX86> hostX86;
                CHECK_ERROR(host, COMGETTER(X86)(hostX86.asOutParam()));
                break;
            }

            default:
                break;
        }
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::host failed", __LINE__);

    ComPtr<ISystemProperties> sysprop;
    RTTestSub(g_hTest, "IVirtualBox::systemProperties");
    CHECK_ERROR(pVBox, COMGETTER(SystemProperties)(sysprop.asOutParam()));
    if (SUCCEEDED(hrc))
    {
        RTTestPassed(g_hTest, "IVirtualBox::systemProperties");

        RTTestSub(g_hTest, "IVirtualBox::platform::::platformProperties");
        ComPtr<IPlatformProperties> platformprop;
        CHECK_ERROR(sysprop, COMGETTER(Platform)(platformprop.asOutParam()));
        BOOL fTemp;
        CHECK_ERROR(platformprop, COMGETTER(ExclusiveHwVirt)(&fTemp));
        ULONG ulTemp;
        CHECK_ERROR(platformprop, GetMaxNetworkAdapters(ChipsetType_PIIX3, &ulTemp));
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::systemProperties failed", __LINE__);

    /*
     * VM
     */
    RTTestSub(g_hTest, "IVirtualBox::CreateMachine");
    ComPtr<IMachine> ptrMachine;
    com::SafeArray<BSTR> groups;
    /** Default VM settings */
    CHECK_ERROR(pVBox, CreateMachine(NULL,                          /** Settings */
                                     tstMachineName.raw(),          /** Name */
                                     enmPlatformArch,
                                     ComSafeArrayAsInParam(groups), /** Groups */
                                     NULL,                          /** OS Type */
                                     NULL,                          /** Create flags */
                                     NULL,                          /** Cipher */
                                     NULL,                          /** Password id */
                                     NULL,                          /** Password */
                                     ptrMachine.asOutParam()));     /** Machine */
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::CreateMachine");
    else
    {
        RTTestFailed(g_hTest, "%d: IVirtualBox::CreateMachine failed", __LINE__);
        return;
    }

    RTTestSub(g_hTest, "IVirtualBox::RegisterMachine");
    CHECK_ERROR(pVBox, RegisterMachine(ptrMachine));
    if (SUCCEEDED(hrc))
        RTTestPassed(g_hTest, "IVirtualBox::RegisterMachine");
    else
    {
        RTTestFailed(g_hTest, "%d: IVirtualBox::RegisterMachine failed", __LINE__);
        return;
    }

    com::SafeIfaceArray<IMachine> machines;
    RTTestSub(g_hTest, "IVirtualBox::machines2");
    CHECK_ERROR(pVBox, COMGETTER(Machines)(ComSafeArrayAsOutParam(machines)));
    if (SUCCEEDED(hrc))
    {
        bool fFound = FALSE;
        for (size_t i = 0; i < machines.size(); ++i)
        {
            if (machines[i])
            {
                Bstr tmpName;
                hrc = machines[i]->COMGETTER(Name)(tmpName.asOutParam());
                if (SUCCEEDED(hrc))
                {
                    if (tmpName == tstMachineName)
                    {
                        fFound = TRUE;
                        break;
                    }
                }
            }
        }

        if (fFound)
            RTTestPassed(g_hTest, "IVirtualBox::machines");
        else
            RTTestFailed(g_hTest, "%d: IVirtualBox::machines failed. No created machine found", __LINE__);
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::machines failed", __LINE__);

    RTTestSub(g_hTest, "IMachine::FirmwareSettings");
    ComPtr<IFirmwareSettings> ptrFirmwareSettings;
    CHECK_ERROR(ptrMachine, COMGETTER(FirmwareSettings)(ptrFirmwareSettings.asOutParam()));
    FirmwareType_T firmwareType;
    CHECK_ERROR(ptrFirmwareSettings, COMGETTER(FirmwareType)(&firmwareType));

//    CHECK_ERROR(ptrMachine, LockMachine());
//    RTTestSub(g_hTest, "IMachine::saveSettings");
//    CHECK_ERROR(ptrMachine, SaveSettings());

    Bstr bstSettingsFile;
    CHECK_ERROR(ptrMachine, COMGETTER(SettingsFilePath)(bstSettingsFile.asOutParam()));
    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Settings file: %ls\n", bstSettingsFile.raw());
//    CHECK_ERROR(ptrMachine, UnlockMachine());

#if 0 /** Not yet implemented */
    com::SafeIfaceArray<ISharedFolder> sharedFolders;
    RTTestSub(g_hTest, "IVirtualBox::sharedFolders");
    CHECK_ERROR(pVBox, COMGETTER(SharedFolders)(ComSafeArrayAsOutParam(sharedFolders)));
    if (SUCCEEDED(rc))
    {
        /** @todo Add ISharedFolders testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::sharedFolders");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::sharedFolders failed", __LINE__);
#endif

    com::SafeIfaceArray<IMedium> hardDisks;
    RTTestSub(g_hTest, "IVirtualBox::hardDisks");
    CHECK_ERROR(pVBox, COMGETTER(HardDisks)(ComSafeArrayAsOutParam(hardDisks)));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add hardDisks testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::hardDisks");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::hardDisks failed", __LINE__);

    com::SafeIfaceArray<IMedium> DVDImages;
    RTTestSub(g_hTest, "IVirtualBox::DVDImages");
    CHECK_ERROR(pVBox, COMGETTER(DVDImages)(ComSafeArrayAsOutParam(DVDImages)));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add DVDImages testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::DVDImages");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::DVDImages failed", __LINE__);

    com::SafeIfaceArray<IMedium> floppyImages;
    RTTestSub(g_hTest, "IVirtualBox::floppyImages");
    CHECK_ERROR(pVBox, COMGETTER(FloppyImages)(ComSafeArrayAsOutParam(floppyImages)));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add floppyImages testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::floppyImages");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::floppyImages failed", __LINE__);

    com::SafeIfaceArray<IProgress> progressOperations;
    RTTestSub(g_hTest, "IVirtualBox::progressOperations");
    CHECK_ERROR(pVBox, COMGETTER(ProgressOperations)(ComSafeArrayAsOutParam(progressOperations)));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add IProgress testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::progressOperations");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::progressOperations failed", __LINE__);

    ComPtr<IPerformanceCollector> performanceCollector;
    RTTestSub(g_hTest, "IVirtualBox::performanceCollector");
    CHECK_ERROR(pVBox, COMGETTER(PerformanceCollector)(performanceCollector.asOutParam()));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add IPerformanceCollector testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::performanceCollector");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::performanceCollector failed", __LINE__);

    com::SafeIfaceArray<IDHCPServer> DHCPServers;
    RTTestSub(g_hTest, "IVirtualBox::DHCPServers");
    CHECK_ERROR(pVBox, COMGETTER(DHCPServers)(ComSafeArrayAsOutParam(DHCPServers)));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add IDHCPServers testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::DHCPServers");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::DHCPServers failed", __LINE__);

    com::SafeIfaceArray<INATNetwork> NATNetworks;
    RTTestSub(g_hTest, "IVirtualBox::NATNetworks");
    CHECK_ERROR(pVBox, COMGETTER(NATNetworks)(ComSafeArrayAsOutParam(NATNetworks)));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add INATNetworks testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::NATNetworks");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::NATNetworks failed", __LINE__);

    ComPtr<IEventSource> eventSource;
    RTTestSub(g_hTest, "IVirtualBox::eventSource");
    CHECK_ERROR(pVBox, COMGETTER(EventSource)(eventSource.asOutParam()));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add IEventSource testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::eventSource");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::eventSource failed", __LINE__);

    ComPtr<IExtPackManager> extensionPackManager;
    RTTestSub(g_hTest, "IVirtualBox::extensionPackManager");
    CHECK_ERROR(pVBox, COMGETTER(ExtensionPackManager)(extensionPackManager.asOutParam()));
    if (SUCCEEDED(hrc))
    {
        /** @todo Add IExtPackManager testing here. */
        RTTestPassed(g_hTest, "IVirtualBox::extensionPackManager");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::extensionPackManager failed", __LINE__);

    com::SafeArray<BSTR> internalNetworks;
    RTTestSub(g_hTest, "IVirtualBox::internalNetworks");
    CHECK_ERROR(pVBox, COMGETTER(InternalNetworks)(ComSafeArrayAsOutParam(internalNetworks)));
    if (SUCCEEDED(hrc))
    {
        RTTestPassed(g_hTest, "IVirtualBox::internalNetworks");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::internalNetworks failed", __LINE__);

    com::SafeArray<BSTR> genericNetworkDrivers;
    RTTestSub(g_hTest, "IVirtualBox::genericNetworkDrivers");
    CHECK_ERROR(pVBox, COMGETTER(GenericNetworkDrivers)(ComSafeArrayAsOutParam(genericNetworkDrivers)));
    if (SUCCEEDED(hrc))
    {
        RTTestPassed(g_hTest, "IVirtualBox::genericNetworkDrivers");
    }
    else
        RTTestFailed(g_hTest, "%d: IVirtualBox::genericNetworkDrivers failed", __LINE__);
}


static void tstApiClean(IVirtualBox *pVBox)
{
    HRESULT hrc;

    /** Delete created VM and its files */
    ComPtr<IMachine> machine;
    CHECK_ERROR(pVBox, FindMachine(Bstr(tstMachineName).raw(), machine.asOutParam()));
    SafeIfaceArray<IMedium> media;
    CHECK_ERROR(machine, Unregister(CleanupMode_DetachAllReturnHardDisksOnly, ComSafeArrayAsOutParam(media)));
    ComPtr<IProgress> progress;
    CHECK_ERROR(machine, DeleteConfig(ComSafeArrayAsInParam(media), progress.asOutParam()));
    CHECK_ERROR(progress, WaitForCompletion(-1));
}


int main()
{
    /*
     * Initialization.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstVBoxAPI", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    SUPR3Init(NULL); /* Better time support. */
    RTTestBanner(g_hTest);

    RTTestSub(g_hTest, "Initializing COM and singletons");
    HRESULT hrc = com::Initialize();
    if (SUCCEEDED(hrc))
    {
        ComPtr<IVirtualBoxClient> ptrVBoxClient;
        ComPtr<IVirtualBox> ptrVBox;
        hrc = TST_COM_EXPR(ptrVBoxClient.createInprocObject(CLSID_VirtualBoxClient));
        if (SUCCEEDED(hrc))
            hrc = TST_COM_EXPR(ptrVBoxClient->COMGETTER(VirtualBox)(ptrVBox.asOutParam()));
        if (SUCCEEDED(hrc))
        {
            ComPtr<ISession> ptrSession;
            hrc = TST_COM_EXPR(ptrSession.createInprocObject(CLSID_Session));
            if (SUCCEEDED(hrc))
            {
                RTTestSubDone(g_hTest);

                /*
                 * Call test functions.
                 */

                /** Test IVirtualBox interface */
                tstApiIVirtualBox(ptrVBox);

                /** Clean files/configs */
                tstApiClean(ptrVBox);
            }
        }

        ptrVBox.setNull();
        ptrVBoxClient.setNull();
        com::Shutdown();
    }
    else
        RTTestIFailed("com::Initialize failed with hrc=%Rhrc", hrc);
    return RTTestSummaryAndDestroy(g_hTest);
}
