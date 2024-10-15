/* $Id: HostImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation: Host
 */

/*
 * Copyright (C) 2004-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOST

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

// VBoxNetCfg-win.h needs winsock2.h and thus MUST be included before any other
// header file includes Windows.h.
#if defined(RT_OS_WINDOWS) && defined(VBOX_WITH_NETFLT)
# include <VBox/VBoxNetCfg-win.h>
#endif

// for some reason Windows burns in sdk\...\winsock.h if this isn't included first
#include "VBox/com/ptr.h"

#include "HostImpl.h"
#include "HostX86Impl.h"

#ifdef VBOX_WITH_USB
# include "HostUSBDeviceImpl.h"
# include "USBDeviceFilterImpl.h"
# include "USBProxyService.h"
#else
# include "VirtualBoxImpl.h"
#endif // VBOX_WITH_USB

#include "HostNetworkInterfaceImpl.h"
#include "HostVideoInputDeviceImpl.h"
#include "AutoCaller.h"
#include "LoggingNew.h"
#include "Performance.h"
#ifdef VBOX_WITH_UPDATE_AGENT
# include "UpdateAgentImpl.h"
#endif

#include "MediumImpl.h"
#include "HostPower.h"

#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
# include <HostHardwareLinux.h>
#endif

#include <set>

#ifdef VBOX_WITH_RESOURCE_USAGE_API
# include "PerformanceImpl.h"
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

#if defined(RT_OS_DARWIN)
# include <sys/types.h>
# include <sys/sysctl.h>
#endif

#ifdef RT_OS_LINUX
# include <sys/ioctl.h>
# include <errno.h>
# include <net/if.h>
# include <net/if_arp.h>
#endif /* RT_OS_LINUX */

#ifdef RT_OS_SOLARIS
# include <fcntl.h>
# include <unistd.h>
# include <stropts.h>
# include <errno.h>
# include <limits.h>
# include <stdio.h>
# include <libdevinfo.h>
# include <sys/mkdev.h>
# include <sys/scsi/generic/inquiry.h>
# include <net/if.h>
# include <sys/socket.h>
# include <sys/sockio.h>
# include <net/if_arp.h>
# include <net/if.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/cdio.h>
# include <sys/dkio.h>
# include <sys/mnttab.h>
# include <sys/mntent.h>
/* Dynamic loading of libhal on Solaris hosts */
# ifdef VBOX_USE_LIBHAL
#  include "vbox-libhal.h"
extern "C" char *getfullrawname(char *);
# endif
# include "solaris/DynLoadLibSolaris.h"

/**
 * Solaris DVD drive list as returned by getDVDInfoFromDevTree().
 */
typedef struct SOLARISDVD
{
    struct SOLARISDVD *pNext;
    char szDescription[512];
    char szRawDiskPath[PATH_MAX];
} SOLARISDVD;
/** Pointer to a Solaris DVD descriptor. */
typedef SOLARISDVD *PSOLARISDVD;

/** Solaris fixed drive (SSD, HDD, ++) descriptor list entry as returned by the
 * solarisWalkDeviceNodeForFixedDrive callback. */
typedef SOLARISDVD SOLARISFIXEDDISK;
/** Pointer to a Solaris fixed drive (SSD, HDD, ++) descriptor. */
typedef SOLARISFIXEDDISK *PSOLARISFIXEDDISK;


#endif /* RT_OS_SOLARIS */

#ifdef RT_OS_WINDOWS
# define _WIN32_DCOM
# include <iprt/win/windows.h>
# include <shellapi.h>
# define INITGUID
# include <guiddef.h>
# include <devguid.h>
# include <iprt/win/objbase.h>
# include <iprt/win/shlobj.h>
# include <cfgmgr32.h>
# include <tchar.h>
#endif /* RT_OS_WINDOWS */

#ifdef RT_OS_DARWIN
# include "darwin/iokit.h"
#endif

#ifdef RT_OS_SOLARIS
# include <iprt/ctype.h>
#endif
#if defined(RT_OS_SOLARIS) || defined(RT_OS_WINDOWS)
# include <iprt/file.h>
#endif
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/path.h>
#include <iprt/time.h>
#ifdef RT_OS_WINDOWS
# include <iprt/dir.h>
# include <iprt/vfs.h>
# include <iprt/utf16.h>
#endif

#ifdef VBOX_WITH_HOSTNETIF_API
# include "netif.h"
#endif

#include <VBox/usb.h>
#include <VBox/err.h>
#include <VBox/settings.h>
#include <VBox/sup.h>
#include <iprt/x86.h>

#include "VBox/com/MultiResult.h"
#include "VBox/com/array.h"

#include <stdio.h>

#include <algorithm>
#include <iprt/sanitized/string>
#include <vector>

#include "HostDnsService.h"
#include "HostDriveImpl.h"
#include "HostDrivePartitionImpl.h"

////////////////////////////////////////////////////////////////////////////////
//
// Host private data definition
//
////////////////////////////////////////////////////////////////////////////////

struct Host::Data
{
    Data()
        :
          pParent(NULL),
          fDVDDrivesListBuilt(false),
          fFloppyDrivesListBuilt(false),
          fPersistentConfigUpToDate(false)
    {};

    VirtualBox              *pParent;

    HostNetworkInterfaceList llNetIfs;                  // list of network interfaces

#ifdef VBOX_WITH_USB
    USBDeviceFilterList     llChildren;                 // all global USB device filters
    USBDeviceFilterList     llUSBDeviceFilters;         // USB device filters in use by the USB proxy service

    /** Pointer to the USBProxyService object. */
    USBProxyService         *pUSBProxyService;
#endif /* VBOX_WITH_USB */

    // list of host drives; lazily created by getDVDDrives() and getFloppyDrives(),
    // and protected by the medium tree lock handle (including the bools).
    MediaList               llDVDDrives,
                            llFloppyDrives;
    bool                    fDVDDrivesListBuilt,
                            fFloppyDrivesListBuilt;

#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
    /** Object with information about host drives */
    VBoxMainDriveInfo       hostDrives;
#endif

    /** @name Features that can be queried with GetProcessorFeature.
     * @{ */
    bool                    fVTSupported,
                            fLongModeSupported,
                            fPAESupported,
                            fNestedPagingSupported,
                            fUnrestrictedGuestSupported,
                            fNestedHWVirtSupported,
                            fVirtVmsaveVmload,
                            fRecheckVTSupported;

    /** @}  */

    HostPowerService        *pHostPowerService;
    /** Host's DNS information fetching */
    HostDnsMonitorProxy     hostDnsMonitorProxy;

    /** Startup syncing of persistent config in extra data */
    bool                    fPersistentConfigUpToDate;

#ifdef VBOX_WITH_UPDATE_AGENT
    /** Reference to the host update agent. */
    const ComObjPtr<HostUpdateAgent> pUpdateHost;
#endif
    /** Reference to the x86 host specific portions of the host object. */
    const ComObjPtr<HostX86>         pHostX86;
};


////////////////////////////////////////////////////////////////////////////////
//
// Constructor / destructor
//
////////////////////////////////////////////////////////////////////////////////
DEFINE_EMPTY_CTOR_DTOR(Host)

HRESULT Host::FinalConstruct()
{
    return BaseFinalConstruct();
}

void Host::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

/**
 * Initializes the host object.
 *
 * @param aParent   VirtualBox parent object.
 */
HRESULT Host::init(VirtualBox *aParent)
{
    HRESULT hrc;
    LogFlowThisFunc(("aParent=%p\n", aParent));

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    m->pParent = aParent;

    hrc = unconst(m->pHostX86).createObject();
    if (SUCCEEDED(hrc))
        hrc = m->pHostX86->init();
    AssertComRCReturn(hrc, hrc);

#ifdef VBOX_WITH_USB
    /*
     * Create and initialize the USB Proxy Service.
     */
    m->pUSBProxyService = new USBProxyService(this);
    hrc = m->pUSBProxyService->init();
    AssertComRCReturn(hrc, hrc);
#endif /* VBOX_WITH_USB */

#ifdef VBOX_WITH_RESOURCE_USAGE_API
    i_registerMetrics(aParent->i_performanceCollector());
#endif /* VBOX_WITH_RESOURCE_USAGE_API */
    /* Create the list of network interfaces so their metrics get registered. */
    i_updateNetIfList();

    m->hostDnsMonitorProxy.init(m->pParent);

#ifdef VBOX_WITH_UPDATE_AGENT
    hrc = unconst(m->pUpdateHost).createObject();
    if (SUCCEEDED(hrc))
        hrc = m->pUpdateHost->init(m->pParent);
    AssertComRCReturn(hrc, hrc);
#endif

#if defined(RT_OS_WINDOWS)
    m->pHostPowerService = new HostPowerServiceWin(m->pParent);
#elif defined(RT_OS_LINUX) && defined(VBOX_WITH_DBUS)
    m->pHostPowerService = new HostPowerServiceLinux(m->pParent);
#elif defined(RT_OS_DARWIN)
    m->pHostPowerService = new HostPowerServiceDarwin(m->pParent);
#else
    m->pHostPowerService = new HostPowerService(m->pParent);
#endif

    /* Cache the features reported by GetProcessorFeature. */
    m->fVTSupported = false;
    m->fLongModeSupported = false;
    m->fPAESupported = false;
    m->fNestedPagingSupported = false;
    m->fUnrestrictedGuestSupported = false;
    m->fNestedHWVirtSupported = false;
    m->fVirtVmsaveVmload = false;
    m->fRecheckVTSupported = false;

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    if (ASMHasCpuId())
    {
        /* Note! This code is duplicated in SUPDrv.c and other places! */
        uint32_t uMaxId, uVendorEBX, uVendorECX, uVendorEDX;
        ASMCpuId(0, &uMaxId, &uVendorEBX, &uVendorECX, &uVendorEDX);
        if (RTX86IsValidStdRange(uMaxId))
        {
            /* PAE? */
            uint32_t uDummy, fFeaturesEcx, fFeaturesEdx;
            ASMCpuId(1, &uDummy, &uDummy, &fFeaturesEcx, &fFeaturesEdx);
            m->fPAESupported = RT_BOOL(fFeaturesEdx & X86_CPUID_FEATURE_EDX_PAE);

            /* Long Mode? */
            uint32_t uExtMaxId, fExtFeaturesEcx, fExtFeaturesEdx;
            ASMCpuId(0x80000000, &uExtMaxId, &uDummy, &uDummy, &uDummy);
            ASMCpuId(0x80000001, &uDummy, &uDummy, &fExtFeaturesEcx, &fExtFeaturesEdx);
            m->fLongModeSupported = RTX86IsValidExtRange(uExtMaxId)
                                 && (fExtFeaturesEdx & X86_CPUID_EXT_FEATURE_EDX_LONG_MODE);

# if defined(RT_OS_DARWIN) && ARCH_BITS == 32 /* darwin.x86 has some optimizations of 64-bit on 32-bit. */
            int     f64bitCapable = 0;
            size_t  cbParameter   = sizeof(f64bitCapable);
            if (sysctlbyname("hw.cpu64bit_capable", &f64bitCapable, &cbParameter, NULL, NULL) != -1)
                m->fLongModeSupported = f64bitCapable != 0;
# endif

            /* VT-x? */
            if (   RTX86IsIntelCpu(uVendorEBX, uVendorECX, uVendorEDX)
                || RTX86IsViaCentaurCpu(uVendorEBX, uVendorECX, uVendorEDX)
                || RTX86IsShanghaiCpu(uVendorEBX, uVendorECX, uVendorEDX))
            {
                if (    (fFeaturesEcx & X86_CPUID_FEATURE_ECX_VMX)
                     && (fFeaturesEdx & X86_CPUID_FEATURE_EDX_MSR)
                     && (fFeaturesEdx & X86_CPUID_FEATURE_EDX_FXSR)
                   )
                {
                    const char *pszIgn;
                    int vrc = SUPR3QueryVTxSupported(&pszIgn);
                    if (RT_SUCCESS(vrc))
                        m->fVTSupported = true;
                }
            }
            /* AMD-V */
            else if (   RTX86IsAmdCpu(uVendorEBX, uVendorECX, uVendorEDX)
                     || RTX86IsHygonCpu(uVendorEBX, uVendorECX, uVendorEDX))
            {
                if (   (fExtFeaturesEcx & X86_CPUID_AMD_FEATURE_ECX_SVM)
                    && (fFeaturesEdx    & X86_CPUID_FEATURE_EDX_MSR)
                    && (fFeaturesEdx    & X86_CPUID_FEATURE_EDX_FXSR)
                    && RTX86IsValidExtRange(uExtMaxId)
                   )
                {
                    m->fVTSupported = true;
                    m->fUnrestrictedGuestSupported = true;

                    /* Query AMD features. */
                    if (uExtMaxId >= 0x8000000a)
                    {
                        uint32_t fSVMFeaturesEdx;
                        ASMCpuId(0x8000000a, &uDummy, &uDummy, &uDummy, &fSVMFeaturesEdx);
                        if (fSVMFeaturesEdx & X86_CPUID_SVM_FEATURE_EDX_NESTED_PAGING)
                            m->fNestedPagingSupported = true;
                        if (fSVMFeaturesEdx & X86_CPUID_SVM_FEATURE_EDX_VIRT_VMSAVE_VMLOAD)
                            m->fVirtVmsaveVmload = true;
                    }
                }
            }
        }
    }
#elif defined(RT_ARCH_ARM64)
    m->fLongModeSupported = true; /* Misnomer but means 64-bit guest support, we are running on 64-bit so it must be available. */

# if defined(RT_OS_DARWIN)
    /*
     * The kern.hv_support parameter indicates support for the hypervisor API in the
     * kernel, which is the only way for virtualization on macOS on Apple Silicon.
     */
    int32_t fHvSupport = 0;
    size_t  cbOld = sizeof(fHvSupport);
    if (sysctlbyname("kern.hv_support", &fHvSupport, &cbOld, NULL, 0) == 0)
    {
        if (fHvSupport != 0)
        {
            m->fVTSupported                = true;
            m->fUnrestrictedGuestSupported = true;
            m->fNestedPagingSupported      = true;
        }
    }
# endif
#endif /* defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) */


    /* Check with SUPDrv if VT-x and AMD-V are really supported (may fail). */
    if (m->fVTSupported)
    {
        m->fRecheckVTSupported = true; /* Try again later when the driver is loaded; cleared by i_updateProcessorFeatures on success. */
        i_updateProcessorFeatures();
    }

    /* Check for NEM in root paritition (hyper-V / windows). */
    if (!m->fVTSupported && SUPR3IsNemSupportedWhenNoVtxOrAmdV())
    {
        m->fVTSupported = m->fNestedPagingSupported = true;
        m->fRecheckVTSupported = false;
    }

#if defined(VBOX_WITH_HOSTNETIF_API) && (defined(RT_OS_LINUX) || defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD))
    /* Extract the list of configured host-only interfaces */
    std::set<Utf8Str> aConfiguredNames;
    SafeArray<BSTR> aGlobalExtraDataKeys;
    hrc = aParent->GetExtraDataKeys(ComSafeArrayAsOutParam(aGlobalExtraDataKeys));
    AssertMsg(SUCCEEDED(hrc), ("VirtualBox::GetExtraDataKeys failed with %Rhrc\n", hrc));
    for (size_t i = 0; i < aGlobalExtraDataKeys.size(); ++i)
    {
        Utf8Str strKey = aGlobalExtraDataKeys[i];

        if (!strKey.startsWith("HostOnly/vboxnet"))
            continue;

        size_t pos = strKey.find("/", sizeof("HostOnly/vboxnet"));
        if (pos != Utf8Str::npos)
            aConfiguredNames.insert(strKey.substr(sizeof("HostOnly"),
                                                  pos - sizeof("HostOnly")));
    }

    for (std::set<Utf8Str>::const_iterator it = aConfiguredNames.begin();
         it != aConfiguredNames.end();
         ++it)
    {
        ComPtr<IHostNetworkInterface> hif;
        ComPtr<IProgress> progress;

        int vrc = NetIfCreateHostOnlyNetworkInterface(m->pParent,
                                                      hif.asOutParam(),
                                                      progress.asOutParam(),
                                                      it->c_str());
        if (RT_FAILURE(vrc))
            LogRel(("failed to create %s, error (%Rrc)\n", it->c_str(), vrc));
    }

#endif /* defined(VBOX_WITH_HOSTNETIF_API) && (defined(RT_OS_LINUX) || defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)) */

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the host object and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void Host::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

#ifdef VBOX_WITH_RESOURCE_USAGE_API
    PerformanceCollector *aCollector = m->pParent->i_performanceCollector();
    i_unregisterMetrics(aCollector);
#endif /* VBOX_WITH_RESOURCE_USAGE_API */
    /*
     * Note that unregisterMetrics() has unregistered all metrics associated
     * with Host including network interface ones. We can destroy network
     * interface objects now. Don't forget the uninit call, otherwise this
     * causes a race with crashing API clients getting their stale references
     * cleaned up and VirtualBox shutting down.
     */
    while (!m->llNetIfs.empty())
    {
        ComObjPtr<HostNetworkInterface> &pNet = m->llNetIfs.front();
        pNet->uninit();
        m->llNetIfs.pop_front();
    }

    m->hostDnsMonitorProxy.uninit();

    if (m->pHostX86)
    {
        m->pHostX86->uninit();
        unconst(m->pHostX86).setNull();
    }

#ifdef VBOX_WITH_UPDATE_AGENT
    if (m->pUpdateHost)
    {
        m->pUpdateHost->uninit();
        unconst(m->pUpdateHost).setNull();
    }
#endif

#ifdef VBOX_WITH_USB
    /* wait for USB proxy service to terminate before we uninit all USB
     * devices */
    LogFlowThisFunc(("Stopping USB proxy service...\n"));
    delete m->pUSBProxyService;
    m->pUSBProxyService = NULL;
    LogFlowThisFunc(("Done stopping USB proxy service.\n"));
#endif

    delete m->pHostPowerService;

#ifdef VBOX_WITH_USB
    /* Clean up the list of global USB device filters. */
    if (!m->llChildren.empty())
    {
        /*
         * i_removeChild() modifies llChildren so we make a copy to traverse here. The
         * removal of a global USB device filter from the llChildren list at this point
         * in Host:uninit() will trigger HostUSBDeviceFilter::FinalRelease() ->
         * HostUSBDeviceFilter::uninit() which will complete the remainder of the clean-up
         * for each global USB device filter and thus we don't need to call
         * HostUSBDeviceFilter::uninit() directly here ourselves.
         */
        USBDeviceFilterList llChildrenCopy(m->llChildren);
        for (USBDeviceFilterList::iterator it = llChildrenCopy.begin();
            it != llChildrenCopy.end();
            ++it)
            i_removeChild(*it);
    }

    /* No need to uninit these, as either Machine::uninit() or the above loop
     * already covered them all. Subset of llChildren. */
    m->llUSBDeviceFilters.clear();
#endif

    /* uninit all host DVD medium objects */
    while (!m->llDVDDrives.empty())
    {
        ComObjPtr<Medium> &pMedium = m->llDVDDrives.front();
        pMedium->uninit();
        m->llDVDDrives.pop_front();
    }
    /* uninit all host floppy medium objects */
    while (!m->llFloppyDrives.empty())
    {
        ComObjPtr<Medium> &pMedium = m->llFloppyDrives.front();
        pMedium->uninit();
        m->llFloppyDrives.pop_front();
    }

    delete m;
    m = NULL;
}

////////////////////////////////////////////////////////////////////////////////
//
// IHost public methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Returns the host's platform architecture.
 *
 * @returns COM status code
 * @param   platformArchitecture    Where to return the host's platform architecture.
 */
HRESULT Host::getArchitecture(PlatformArchitecture_T *platformArchitecture)
{
    *platformArchitecture = Host::s_getPlatformArchitecture();

    return S_OK;
}

/**
 * Returns a list of host DVD drives.
 *
 * @returns COM status code
 * @param aDVDDrives    address of result pointer
 */

HRESULT Host::getDVDDrives(std::vector<ComPtr<IMedium> > &aDVDDrives)
{
    AutoWriteLock treeLock(m->pParent->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    MediaList *pList;
    HRESULT hrc = i_getDrives(DeviceType_DVD, true /* fRefresh */, pList, treeLock);
    if (FAILED(hrc))
        return hrc;

    aDVDDrives.resize(pList->size());
    size_t i = 0;
    for (MediaList::const_iterator it = pList->begin(); it != pList->end(); ++it, ++i)
        (*it).queryInterfaceTo(aDVDDrives[i].asOutParam());

    return S_OK;
}

/**
 * Returns a list of host floppy drives.
 *
 * @returns COM status code
 * @param   aFloppyDrives   address of result pointer
 */
HRESULT Host::getFloppyDrives(std::vector<ComPtr<IMedium> > &aFloppyDrives)
{
    AutoWriteLock treeLock(m->pParent->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    MediaList *pList;
    HRESULT hrc = i_getDrives(DeviceType_Floppy, true /* fRefresh */, pList, treeLock);
    if (FAILED(hrc))
        return hrc;

    aFloppyDrives.resize(pList->size());
    size_t i = 0;
    for (MediaList::const_iterator it = pList->begin(); it != pList->end(); ++it, ++i)
        (*it).queryInterfaceTo(aFloppyDrives[i].asOutParam());

    return S_OK;
}


#if defined(RT_OS_WINDOWS) && defined(VBOX_WITH_NETFLT)
# define VBOX_APP_NAME L"VirtualBox"

static int vboxNetWinAddComponent(std::list< ComObjPtr<HostNetworkInterface> > *pPist,
                                  INetCfgComponent *pncc)
{
    int vrc = VERR_GENERAL_FAILURE;

    LPWSTR lpszName = NULL;
    HRESULT hrc = pncc->GetDisplayName(&lpszName);
    Assert(hrc == S_OK);
    if (hrc == S_OK)
    {
        Bstr name((CBSTR)lpszName);

        GUID IfGuid;
        hrc = pncc->GetInstanceGuid(&IfGuid);
        Assert(hrc == S_OK);
        if (hrc == S_OK)
        {
            /* create a new object and add it to the list */
            ComObjPtr<HostNetworkInterface> iface;
            iface.createObject();
            /* remove the curly bracket at the end */
            if (SUCCEEDED(iface->init(name, name, Guid(IfGuid), HostNetworkInterfaceType_Bridged)))
            {
//                iface->setVirtualBox(m->pParent);
                pPist->push_back(iface);
                vrc = VINF_SUCCESS;
            }
            else
            {
                Assert(0);
            }
        }
        CoTaskMemFree(lpszName);
    }

    return vrc;
}
#endif /* defined(RT_OS_WINDOWS) && defined(VBOX_WITH_NETFLT) */

#if defined(RT_OS_WINDOWS)
struct HostOnlyInfo
{
    HostOnlyInfo() : fDhcpEnabled(false), uIPv6PrefixLength(0) {};

    Bstr bstrName;
    bool fDhcpEnabled;
    Bstr strIPv4Address;
    Bstr strIPv4NetMask;
    Bstr strIPv6Address;
    ULONG uIPv6PrefixLength;
};

typedef std::map<Utf8Str, HostOnlyInfo*> GUID_TO_HOST_ONLY_INFO;

HRESULT Host::i_updatePersistentConfigForHostOnlyAdapters(void)
{
    /* No need to do the sync twice */
    if (m->fPersistentConfigUpToDate)
        return S_OK;
    m->fPersistentConfigUpToDate = true;
    bool fChangesMade = false;

    /* Extract the list of configured host-only interfaces */
    GUID_TO_HOST_ONLY_INFO aSavedAdapters;
    SafeArray<BSTR> aGlobalExtraDataKeys;
    HRESULT hrc = m->pParent->GetExtraDataKeys(ComSafeArrayAsOutParam(aGlobalExtraDataKeys));
    AssertMsg(SUCCEEDED(hrc), ("VirtualBox::GetExtraDataKeys failed with %Rhrc\n", hrc));
    for (size_t i = 0; i < aGlobalExtraDataKeys.size(); ++i)
    {
        Utf8Str strKey = aGlobalExtraDataKeys[i];

        if (strKey.startsWith("HostOnly/{"))
        {
            Bstr bstrValue;
            hrc = m->pParent->GetExtraData(aGlobalExtraDataKeys[i], bstrValue.asOutParam());
            if (hrc != S_OK)
                continue;

            Utf8Str strGuid = strKey.substr(10, 36); /* Skip "HostOnly/{" */
            if (aSavedAdapters.find(strGuid) == aSavedAdapters.end())
                aSavedAdapters[strGuid] = new HostOnlyInfo();

            if (strKey.endsWith("}/Name"))
                aSavedAdapters[strGuid]->bstrName = bstrValue;
            else if (strKey.endsWith("}/IPAddress"))
            {
                if (bstrValue == "DHCP")
                    aSavedAdapters[strGuid]->fDhcpEnabled = true;
                else
                    aSavedAdapters[strGuid]->strIPv4Address = bstrValue;
            }
            else if (strKey.endsWith("}/IPNetMask"))
                aSavedAdapters[strGuid]->strIPv4NetMask = bstrValue;
            else if (strKey.endsWith("}/IPV6Address"))
                aSavedAdapters[strGuid]->strIPv6Address = bstrValue;
            else if (strKey.endsWith("}/IPV6PrefixLen"))
                aSavedAdapters[strGuid]->uIPv6PrefixLength = Utf8Str(bstrValue).toUInt32();
        }
    }

    /* Go over the list of existing adapters and update configs saved in extra data */
    std::set<Bstr> aKnownNames;
    for (HostNetworkInterfaceList::iterator it = m->llNetIfs.begin(); it != m->llNetIfs.end(); ++it)
    {
        /* Get type */
        HostNetworkInterfaceType_T t;
        hrc = (*it)->COMGETTER(InterfaceType)(&t);
        if (FAILED(hrc) || t != HostNetworkInterfaceType_HostOnly)
            continue;
        /* Get id */
        Bstr bstrGuid;
        hrc = (*it)->COMGETTER(Id)(bstrGuid.asOutParam());
        if (FAILED(hrc))
            continue;
        /* Get name */
        Bstr bstrName;
        hrc = (*it)->COMGETTER(Name)(bstrName.asOutParam());
        if (FAILED(hrc))
            continue;

        /* Remove adapter from map as it does not need any further processing */
        aSavedAdapters.erase(Utf8Str(bstrGuid));
        /* Add adapter name to the list of known names, so we won't attempt to create adapters with the same name */
        aKnownNames.insert(bstrName);
        /* Make sure our extra data contains the latest config */
        hrc = (*it)->i_updatePersistentConfig();
        if (hrc != S_OK)
            break;
    }

    /* The following loop not only creates missing adapters, it destroys HostOnlyInfo objects contained in the map as well */
    for (GUID_TO_HOST_ONLY_INFO::iterator it = aSavedAdapters.begin(); it != aSavedAdapters.end(); ++it)
    {
        Utf8Str strGuid = (*it).first;
        HostOnlyInfo *pInfo = (*it).second;
        /* We create adapters only if we haven't seen one with the same name */
        if (aKnownNames.find(pInfo->bstrName) == aKnownNames.end())
        {
            /* There is no adapter with such name yet, create it */
            ComPtr<IHostNetworkInterface> hif;
            ComPtr<IProgress> progress;

            int vrc = NetIfCreateHostOnlyNetworkInterface(m->pParent, hif.asOutParam(), progress.asOutParam(),
                                                          pInfo->bstrName.raw());
            if (RT_FAILURE(vrc))
            {
                LogRel(("Failed to create host-only adapter (%Rrc)\n", vrc));
                hrc = E_UNEXPECTED;
                break;
            }

            /* Wait for the adapter to get configured completely, before we modify IP addresses. */
            progress->WaitForCompletion(-1);
            fChangesMade = true;
            if (pInfo->fDhcpEnabled)
            {
                hrc = hif->EnableDynamicIPConfig();
                if (FAILED(hrc))
                    LogRel(("EnableDynamicIPConfig failed with 0x%x\n", hrc));
            }
            else
            {
                hrc = hif->EnableStaticIPConfig(pInfo->strIPv4Address.raw(), pInfo->strIPv4NetMask.raw());
                if (FAILED(hrc))
                    LogRel(("EnableStaticIpConfig failed with 0x%x\n", hrc));
            }
# if 0
            /* Somehow HostNetworkInterface::EnableStaticIPConfigV6 is not implemented yet. */
            if (SUCCEEDED(hrc))
            {
                hrc = hif->EnableStaticIPConfigV6(pInfo->strIPv6Address.raw(), pInfo->uIPv6PrefixLength);
                if (FAILED(hrc))
                    LogRel(("EnableStaticIPConfigV6 failed with 0x%x\n", hrc));
            }
# endif
            /* Now we have seen this name */
            aKnownNames.insert(pInfo->bstrName);
            /* Drop the old config as the newly created adapter has different GUID */
            i_removePersistentConfig(strGuid);
        }
        delete pInfo;
    }
    /* Update the list again if we have created some adapters */
    if (SUCCEEDED(hrc) && fChangesMade)
        hrc = i_updateNetIfList();

    return hrc;
}
#endif /* defined(RT_OS_WINDOWS) */

/**
 * Returns a list of host network interfaces.
 *
 * @returns COM status code
 * @param   aNetworkInterfaces  address of result pointer
 */
HRESULT Host::getNetworkInterfaces(std::vector<ComPtr<IHostNetworkInterface> > &aNetworkInterfaces)
{
#if defined(RT_OS_WINDOWS) || defined(VBOX_WITH_NETFLT) /*|| defined(RT_OS_OS2)*/
# ifdef VBOX_WITH_HOSTNETIF_API
    HRESULT hrc = i_updateNetIfList();
    if (FAILED(hrc))
    {
        Log(("Failed to update host network interface list with hrc=%Rhrc\n", hrc));
        return hrc;
    }
#if defined(RT_OS_WINDOWS)
    hrc = i_updatePersistentConfigForHostOnlyAdapters();
    if (FAILED(hrc))
    {
        LogRel(("Failed to update persistent config for host-only adapters with hrc=%Rhrc\n", hrc));
        return hrc;
    }
#endif /* defined(RT_OS_WINDOWS) */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aNetworkInterfaces.resize(m->llNetIfs.size());
    size_t i = 0;
    for (HostNetworkInterfaceList::iterator it = m->llNetIfs.begin(); it != m->llNetIfs.end(); ++it, ++i)
        (*it).queryInterfaceTo(aNetworkInterfaces[i].asOutParam());

    return S_OK;
# else
    std::list<ComObjPtr<HostNetworkInterface> > list;

#  if defined(RT_OS_DARWIN)
    PDARWINETHERNIC pEtherNICs = DarwinGetEthernetControllers();
    while (pEtherNICs)
    {
        ComObjPtr<HostNetworkInterface> IfObj;
        IfObj.createObject();
        if (SUCCEEDED(IfObj->init(Bstr(pEtherNICs->szName), Guid(pEtherNICs->Uuid), HostNetworkInterfaceType_Bridged)))
            list.push_back(IfObj);

        /* next, free current */
        void *pvFree = pEtherNICs;
        pEtherNICs = pEtherNICs->pNext;
        RTMemFree(pvFree);
    }

#  elif defined RT_OS_WINDOWS
#   ifndef VBOX_WITH_NETFLT
    hrc = E_NOTIMPL;
#   else /* #  if defined VBOX_WITH_NETFLT */
    INetCfg              *pNc;
    INetCfgComponent     *pMpNcc;
    INetCfgComponent     *pTcpIpNcc;
    LPWSTR               lpszApp;
    HRESULT              hrc;
    IEnumNetCfgBindingPath      *pEnumBp;
    INetCfgBindingPath          *pBp;
    IEnumNetCfgBindingInterface *pEnumBi;
    INetCfgBindingInterface *pBi;

    /* we are using the INetCfg API for getting the list of miniports */
    hrc = VBoxNetCfgWinQueryINetCfg(FALSE, VBOX_APP_NAME, &pNc, &lpszApp);
    Assert(hrc == S_OK);
    if (hrc == S_OK)
    {
#    ifdef VBOX_NETFLT_ONDEMAND_BIND
        /* for the protocol-based approach for now we just get all miniports the MS_TCPIP protocol binds to */
        hrc = pNc->FindComponent(L"MS_TCPIP", &pTcpIpNcc);
#    else
        /* for the filter-based approach we get all miniports our filter (oracle_VBoxNetLwf)is bound to */
        hrc = pNc->FindComponent(L"oracle_VBoxNetLwf", &pTcpIpNcc);
        if (hrc != S_OK)
        {
            /* fall back to NDIS5 miniport lookup (sun_VBoxNetFlt) */
            hrc = pNc->FindComponent(L"sun_VBoxNetFlt", &pTcpIpNcc);
        }
#     ifndef VBOX_WITH_HARDENING
        if (hrc != S_OK)
        {
            /** @todo try to install the netflt from here */
        }
#     endif

#    endif

        if (hrc == S_OK)
        {
            hrc = VBoxNetCfgWinGetBindingPathEnum(pTcpIpNcc, EBP_BELOW, &pEnumBp);
            Assert(hrc == S_OK);
            if (hrc == S_OK)
            {
                hrc = VBoxNetCfgWinGetFirstBindingPath(pEnumBp, &pBp);
                Assert(hrc == S_OK || hrc == S_FALSE);
                while (hrc == S_OK)
                {
                    /* S_OK == enabled, S_FALSE == disabled */
                    if (pBp->IsEnabled() == S_OK)
                    {
                        hrc = VBoxNetCfgWinGetBindingInterfaceEnum(pBp, &pEnumBi);
                        Assert(hrc == S_OK);
                        if (hrc == S_OK)
                        {
                            hrc = VBoxNetCfgWinGetFirstBindingInterface(pEnumBi, &pBi);
                            Assert(hrc == S_OK);
                            while (hrc == S_OK)
                            {
                                hrc = pBi->GetLowerComponent(&pMpNcc);
                                Assert(hrc == S_OK);
                                if (hrc == S_OK)
                                {
                                    ULONG uComponentStatus;
                                    hrc = pMpNcc->GetDeviceStatus(&uComponentStatus);
                                    Assert(hrc == S_OK);
                                    if (hrc == S_OK)
                                    {
                                        if (uComponentStatus == 0)
                                            vboxNetWinAddComponent(&list, pMpNcc);
                                    }
                                    VBoxNetCfgWinReleaseRef(pMpNcc);
                                }
                                VBoxNetCfgWinReleaseRef(pBi);

                                hrc = VBoxNetCfgWinGetNextBindingInterface(pEnumBi, &pBi);
                            }
                            VBoxNetCfgWinReleaseRef(pEnumBi);
                        }
                    }
                    VBoxNetCfgWinReleaseRef(pBp);

                    hrc = VBoxNetCfgWinGetNextBindingPath(pEnumBp, &pBp);
                }
                VBoxNetCfgWinReleaseRef(pEnumBp);
            }
            VBoxNetCfgWinReleaseRef(pTcpIpNcc);
        }
        else
        {
            LogRel(("failed to get the oracle_VBoxNetLwf(sun_VBoxNetFlt) component, error (0x%x)\n", hrc));
        }

        VBoxNetCfgWinReleaseINetCfg(pNc, FALSE);
    }
#   endif /* #  if defined VBOX_WITH_NETFLT */


#  elif defined RT_OS_LINUX
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0)
    {
        char pBuffer[2048];
        struct ifconf ifConf;
        ifConf.ifc_len = sizeof(pBuffer);
        ifConf.ifc_buf = pBuffer;
        if (ioctl(sock, SIOCGIFCONF, &ifConf) >= 0)
        {
            for (struct ifreq *pReq = ifConf.ifc_req; (char*)pReq < pBuffer + ifConf.ifc_len; pReq++)
            {
                if (ioctl(sock, SIOCGIFHWADDR, pReq) >= 0)
                {
                    if (pReq->ifr_hwaddr.sa_family == ARPHRD_ETHER)
                    {
                        RTUUID uuid;
                        Assert(sizeof(uuid) <= sizeof(*pReq));
                        memcpy(&uuid, pReq, sizeof(uuid));

                        ComObjPtr<HostNetworkInterface> IfObj;
                        IfObj.createObject();
                        if (SUCCEEDED(IfObj->init(Bstr(pReq->ifr_name), Guid(uuid), HostNetworkInterfaceType_Bridged)))
                            list.push_back(IfObj);
                    }
                }
            }
        }
        close(sock);
    }
#  endif /* RT_OS_LINUX */

    aNetworkInterfaces.resize(list.size());
    size_t i = 0;
    for (std::list<ComObjPtr<HostNetworkInterface> >::const_iterator it = list.begin(); it != list.end(); ++it, ++i)
        aNetworkInterfaces[i] = *it;

    return S_OK;
# endif
#else
    /* Not implemented / supported on this platform. */
    RT_NOREF(aNetworkInterfaces);
    ReturnComNotImplemented();
#endif
}

HRESULT Host::getAudioDevices(std::vector<ComPtr<IHostAudioDevice> > &aAudioDevices)
{
    RT_NOREF(aAudioDevices);
    ReturnComNotImplemented();
}

HRESULT Host::getUSBDevices(std::vector<ComPtr<IHostUSBDevice> > &aUSBDevices)
{
#ifdef VBOX_WITH_USB
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    MultiResult mrc = i_checkUSBProxyService();
    if (FAILED(mrc) || SUCCEEDED_WARNING(mrc))
        return mrc;

    return m->pUSBProxyService->getDeviceCollection(aUSBDevices);
#else
    /* Note: The GUI depends on this method returning E_NOTIMPL with no
     * extended error info to indicate that USB is simply not available
     * (w/o treating it as a failure), for example, as in OSE. */
    RT_NOREF(aUSBDevices);
    ReturnComNotImplemented();
#endif
}

/**
 * This method return the list of registered name servers
 */
HRESULT Host::getNameServers(std::vector<com::Utf8Str> &aNameServers)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return m->hostDnsMonitorProxy.GetNameServers(aNameServers);
}


/**
 * This method returns the domain name of the host
 */
HRESULT Host::getDomainName(com::Utf8Str &aDomainName)
{
    /** @todo XXX: note here should be synchronization with thread polling state
     * changes in name resolving system on host */
    return m->hostDnsMonitorProxy.GetDomainName(&aDomainName);
}


/**
 * This method returns the search string.
 */
HRESULT Host::getSearchStrings(std::vector<com::Utf8Str> &aSearchStrings)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return m->hostDnsMonitorProxy.GetSearchStrings(aSearchStrings);
}

HRESULT Host::getUSBDeviceFilters(std::vector<ComPtr<IHostUSBDeviceFilter> > &aUSBDeviceFilters)
{
#ifdef VBOX_WITH_USB
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    MultiResult mrc = i_checkUSBProxyService();
    if (FAILED(mrc))
        return mrc;

    aUSBDeviceFilters.resize(m->llUSBDeviceFilters.size());
    size_t i = 0;
    for (USBDeviceFilterList::iterator it = m->llUSBDeviceFilters.begin(); it != m->llUSBDeviceFilters.end(); ++it, ++i)
        (*it).queryInterfaceTo(aUSBDeviceFilters[i].asOutParam());

    return mrc;
#else
    /* Note: The GUI depends on this method returning E_NOTIMPL with no
     * extended error info to indicate that USB is simply not available
     * (w/o treating it as a failure), for example, as in OSE. */
    RT_NOREF(aUSBDeviceFilters);
    ReturnComNotImplemented();
#endif
}

/**
 * Returns the number of installed logical processors
 *
 * @returns COM status code
 * @param   aCount  address of result variable
 */

HRESULT Host::getProcessorCount(ULONG *aCount)
{
    // no locking required

    *aCount = RTMpGetPresentCount();
    return S_OK;
}

/**
 * Returns the number of online logical processors
 *
 * @returns COM status code
 * @param   aCount  address of result variable
 */
HRESULT Host::getProcessorOnlineCount(ULONG *aCount)
{
    // no locking required

    *aCount = RTMpGetOnlineCount();
    return S_OK;
}

/**
 * Returns the number of installed physical processor cores.
 *
 * @returns COM status code
 * @param   aCount  address of result variable
 */
HRESULT Host::getProcessorCoreCount(ULONG *aCount)
{
    // no locking required

    *aCount = RTMpGetPresentCoreCount();
    return S_OK;
}

/**
 * Returns the number of installed physical processor cores.
 *
 * @returns COM status code
 * @param   aCount  address of result variable
 */
HRESULT Host::getProcessorOnlineCoreCount(ULONG *aCount)
{
    // no locking required

    *aCount = RTMpGetOnlineCoreCount();
    return S_OK;
}

/**
 * Returns the (approximate) maximum speed of the given host CPU in MHz
 *
 * @returns COM status code
 * @param   aCpuId  id to get info for.
 * @param   aSpeed  address of result variable, speed is 0 if unknown or aCpuId
 *          is invalid.
 */
HRESULT Host::getProcessorSpeed(ULONG aCpuId,
                                ULONG *aSpeed)
{
    // no locking required

    *aSpeed = RTMpGetMaxFrequency(aCpuId);
    return S_OK;
}

/**
 * Returns a description string for the host CPU
 *
 * @returns COM status code
 * @param   aCpuId  id to get info for.
 * @param   aDescription address of result variable, empty string if not known
 *          or aCpuId is invalid.
 */
HRESULT Host::getProcessorDescription(ULONG aCpuId, com::Utf8Str &aDescription)
{
    // no locking required

    int vrc = aDescription.reserveNoThrow(80);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTMpGetDescription(aCpuId, aDescription.mutableRaw(), aDescription.capacity());
        if (RT_SUCCESS(vrc))
        {
            aDescription.jolt();
            return S_OK;
        }
    }
    return setErrorVrc(vrc);
}

/**
 * Updates fVTSupported, fNestedPagingSupported, fUnrestrictedGuestSupported,
 * fVirtVmsaveVmload and fNestedHWVirtSupported with info from SUPR3QueryVTCaps().
 *
 * This is repeated till we successfully open the support driver, in case it
 * is loaded after VBoxSVC starts.
 */
void Host::i_updateProcessorFeatures()
{
#ifndef RT_ARCH_ARM64
    /* Perhaps the driver is available now... */
    int vrc = SUPR3InitEx(SUPR3INIT_F_LIMITED, NULL);
    if (RT_SUCCESS(vrc))
    {
        uint32_t fVTCaps;
        vrc = SUPR3QueryVTCaps(&fVTCaps);
        AssertMsg(RT_SUCCESS(vrc) || vrc == VERR_SUP_DRIVERLESS, ("SUPR3QueryVTCaps failed vrc=%Rrc\n", vrc));

        SUPR3Term(false);

        AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);
        if (RT_FAILURE(vrc))
        {
            fVTCaps = 0;
            if (vrc != VERR_SUP_DRIVERLESS)
                LogRel(("SUPR0QueryVTCaps -> %Rrc\n", vrc));
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) /* Preserve detected VT-x/AMD-V support for show. */
            else
                fVTCaps = m->fVTSupported ? SUPVTCAPS_AMD_V | SUPVTCAPS_VT_X : 0;
# endif
        }
        m->fVTSupported                = (fVTCaps & (SUPVTCAPS_AMD_V | SUPVTCAPS_VT_X)) != 0;
        m->fNestedPagingSupported      = (fVTCaps & SUPVTCAPS_NESTED_PAGING) != 0;
        m->fUnrestrictedGuestSupported = (fVTCaps & (SUPVTCAPS_AMD_V | SUPVTCAPS_VTX_UNRESTRICTED_GUEST)) != 0;
        m->fNestedHWVirtSupported      =     (fVTCaps & (SUPVTCAPS_AMD_V | SUPVTCAPS_NESTED_PAGING))
                                          ==            (SUPVTCAPS_AMD_V | SUPVTCAPS_NESTED_PAGING)
                                      ||     (fVTCaps & (  SUPVTCAPS_VT_X | SUPVTCAPS_NESTED_PAGING
                                                         | SUPVTCAPS_VTX_UNRESTRICTED_GUEST | SUPVTCAPS_VTX_VMCS_SHADOWING))
                                          ==            (  SUPVTCAPS_VT_X | SUPVTCAPS_NESTED_PAGING
                                                         | SUPVTCAPS_VTX_UNRESTRICTED_GUEST | SUPVTCAPS_VTX_VMCS_SHADOWING);
        m->fVirtVmsaveVmload           = (fVTCaps & SUPVTCAPS_AMDV_VIRT_VMSAVE_VMLOAD) != 0;
        m->fRecheckVTSupported = false; /* No need to try again, we cached everything. */
    }
#endif
}

/**
 * Returns whether a host processor feature is supported or not
 *
 * @returns COM status code
 * @param   aFeature    to query.
 * @param   aSupported  supported bool result variable
 */
HRESULT Host::getProcessorFeature(ProcessorFeature_T aFeature, BOOL *aSupported)
{
    /* Validate input. */
    switch (aFeature)
    {
        case ProcessorFeature_HWVirtEx:
        case ProcessorFeature_PAE:
        case ProcessorFeature_LongMode:
        case ProcessorFeature_NestedPaging:
        case ProcessorFeature_UnrestrictedGuest:
        case ProcessorFeature_NestedHWVirt:
        case ProcessorFeature_VirtVmsaveVmload:
            break;
        default:
            return setError(E_INVALIDARG, tr("The aFeature value %d (%#x) is out of range."), (int)aFeature, (int)aFeature);
    }

    /* Do the job. */
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (   m->fRecheckVTSupported
            && (   aFeature == ProcessorFeature_HWVirtEx
                || aFeature == ProcessorFeature_NestedPaging
                || aFeature == ProcessorFeature_UnrestrictedGuest
                || aFeature == ProcessorFeature_NestedHWVirt
                || aFeature == ProcessorFeature_VirtVmsaveVmload)
           )
        {
            alock.release();
            i_updateProcessorFeatures();
            alock.acquire();
        }

        switch (aFeature)
        {
            case ProcessorFeature_HWVirtEx:
                *aSupported = m->fVTSupported;
                break;

            case ProcessorFeature_PAE:
                *aSupported = m->fPAESupported;
                break;

            case ProcessorFeature_LongMode:
                *aSupported = m->fLongModeSupported;
                break;

            case ProcessorFeature_NestedPaging:
                *aSupported = m->fNestedPagingSupported;
                break;

            case ProcessorFeature_UnrestrictedGuest:
                *aSupported = m->fUnrestrictedGuestSupported;
                break;

            case ProcessorFeature_NestedHWVirt:
                *aSupported = m->fNestedHWVirtSupported;
                break;

            case ProcessorFeature_VirtVmsaveVmload:
                *aSupported = m->fVirtVmsaveVmload;
                break;

            default:
                AssertFailed();
        }
    }
    return hrc;
}

/**
 * Returns the amount of installed system memory in megabytes
 *
 * @returns COM status code
 * @param   aSize   address of result variable
 */
HRESULT Host::getMemorySize(ULONG *aSize)
{
    // no locking required

    uint64_t cb;
    int vrc = RTSystemQueryTotalRam(&cb);
    if (RT_FAILURE(vrc))
        return E_FAIL;
    *aSize = (ULONG)(cb / _1M);
    return S_OK;
}

/**
 * Returns the current system memory free space in megabytes
 *
 * @returns COM status code
 * @param   aAvailable  address of result variable
 */
HRESULT Host::getMemoryAvailable(ULONG *aAvailable)
{
    // no locking required

    uint64_t cb;
    int vrc = RTSystemQueryAvailableRam(&cb);
    if (RT_FAILURE(vrc))
        return E_FAIL;
    *aAvailable = (ULONG)(cb / _1M);
    return S_OK;
}

/**
 * Returns the name string of the host operating system
 *
 * @returns COM status code
 * @param   aOperatingSystem result variable
 */
HRESULT Host::getOperatingSystem(com::Utf8Str &aOperatingSystem)
{
    // no locking required

    char szOSName[80];
    int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szOSName, sizeof(szOSName));
    if (RT_FAILURE(vrc))
        return E_FAIL; /** @todo error reporting? */
    aOperatingSystem = Utf8Str(szOSName);
    return S_OK;
}

/**
 * Returns the version string of the host operating system
 *
 * @returns COM status code
 * @param   aVersion    address of result variable
 */
HRESULT Host::getOSVersion(com::Utf8Str &aVersion)
{
    // no locking required

    /* Get the OS release. Reserve some buffer space for the service pack. */
    char szOSRelease[128];
    int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szOSRelease, sizeof(szOSRelease) - 32);
    if (RT_FAILURE(vrc))
        return E_FAIL; /** @todo error reporting? */

    /* Append the service pack if present. */
    char szOSServicePack[80];
    vrc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szOSServicePack, sizeof(szOSServicePack));
    if (RT_FAILURE(vrc))
    {
        if (vrc != VERR_NOT_SUPPORTED)
            return E_FAIL; /** @todo error reporting? */
        szOSServicePack[0] = '\0';
    }
    if (szOSServicePack[0] != '\0')
    {
        char *psz = strchr(szOSRelease, '\0');
        RTStrPrintf(psz, (size_t)(&szOSRelease[sizeof(szOSRelease)] - psz), "sp%s", szOSServicePack);
    }

    aVersion = szOSRelease;
    return S_OK;
}

/**
 * Returns the current host time in milliseconds since 1970-01-01 UTC.
 *
 * @returns COM status code
 * @param   aUTCTime    address of result variable
 */
HRESULT Host::getUTCTime(LONG64 *aUTCTime)
{
    // no locking required

    RTTIMESPEC now;
    *aUTCTime = RTTimeSpecGetMilli(RTTimeNow(&now));

    return S_OK;
}

HRESULT Host::createHostOnlyNetworkInterface(ComPtr<IHostNetworkInterface> &aHostInterface,
                                             ComPtr<IProgress> &aProgress)

{
#ifdef VBOX_WITH_HOSTNETIF_API
    /* No need to lock anything. If there ever will - watch out, the function
     * called below grabs the VirtualBox lock. */

    int vrc = NetIfCreateHostOnlyNetworkInterface(m->pParent, aHostInterface.asOutParam(), aProgress.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        if (aHostInterface.isNull())
            return setError(E_FAIL,
                            tr("Unable to create a host network interface"));

# if !defined(RT_OS_WINDOWS)
        Bstr tmpAddr, tmpMask, tmpName;
        HRESULT hrc;
        hrc = aHostInterface->COMGETTER(Name)(tmpName.asOutParam());
        ComAssertComRCRet(hrc, hrc);
        hrc = aHostInterface->COMGETTER(IPAddress)(tmpAddr.asOutParam());
        ComAssertComRCRet(hrc, hrc);
        hrc = aHostInterface->COMGETTER(NetworkMask)(tmpMask.asOutParam());
        ComAssertComRCRet(hrc, hrc);

        /*
         * We need to write the default IP address and mask to extra data now,
         * so the interface gets re-created after vboxnetadp.ko reload.
         * Note that we avoid calling EnableStaticIpConfig since it would
         * change the address on host's interface as well and we want to
         * postpone the change until VM actually starts.
         */
        hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/%ls/IPAddress", tmpName.raw()).raw(),
                                       tmpAddr.raw());
        ComAssertComRCRet(hrc, hrc);

        hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/%ls/IPNetMask", tmpName.raw()).raw(),
                                       tmpMask.raw());
        ComAssertComRCRet(hrc, hrc);
# endif /* !defined(RT_OS_WINDOWS) */
    }

    return S_OK;
#else
    RT_NOREF(aHostInterface, aProgress);
    return E_NOTIMPL;
#endif
}


#ifdef RT_OS_WINDOWS
HRESULT Host::i_removePersistentConfig(const Bstr &bstrGuid)
{
    HRESULT hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/{%ls}/Name", bstrGuid.raw()).raw(), NULL);
    if (SUCCEEDED(hrc)) hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/{%ls}/IPAddress", bstrGuid.raw()).raw(), NULL);
    if (SUCCEEDED(hrc)) hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/{%ls}/IPNetMask", bstrGuid.raw()).raw(), NULL);
    if (SUCCEEDED(hrc)) hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/{%ls}/IPV6Address", bstrGuid.raw()).raw(), NULL);
    if (SUCCEEDED(hrc)) hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/{%ls}/IPV6PrefixLen", bstrGuid.raw()).raw(), NULL);
    return hrc;
}
#endif /* RT_OS_WINDOWS */

HRESULT Host::removeHostOnlyNetworkInterface(const com::Guid &aId,
                                             ComPtr<IProgress> &aProgress)

{
#ifdef VBOX_WITH_HOSTNETIF_API
    /* No need to lock anything, the code below does not touch the state
     * of the host object. If that ever changes then check for lock order
     * violations with the called functions. */

    Bstr name;
    HRESULT hrc;

    /* first check whether an interface with the given name already exists */
    {
        ComPtr<IHostNetworkInterface> iface;
        hrc = findHostNetworkInterfaceById(aId, iface);
        if (FAILED(hrc))
            return setError(VBOX_E_OBJECT_NOT_FOUND,
                            tr("Host network interface with UUID {%RTuuid} does not exist"),
                            Guid(aId).raw());
        hrc = iface->COMGETTER(Name)(name.asOutParam());
        ComAssertComRCRet(hrc, hrc);
    }

    int vrc = NetIfRemoveHostOnlyNetworkInterface(m->pParent, aId, aProgress.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        /* Drop configuration parameters for removed interface */
#ifdef RT_OS_WINDOWS
        hrc = i_removePersistentConfig(Utf8StrFmt("%RTuuid", &aId));
        if (FAILED(hrc))
            LogRel(("i_removePersistentConfig(%RTuuid) failed with 0x%x\n", &aId, hrc));
#else /* !RT_OS_WINDOWS */
        hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/%ls/IPAddress", name.raw()).raw(), NULL);
        hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/%ls/IPNetMask", name.raw()).raw(), NULL);
        hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/%ls/IPV6Address", name.raw()).raw(), NULL);
        hrc = m->pParent->SetExtraData(BstrFmt("HostOnly/%ls/IPV6NetMask", name.raw()).raw(), NULL);
#endif /* !RT_OS_WINDOWS */

        return S_OK;
    }

    return vrc == VERR_NOT_IMPLEMENTED ? E_NOTIMPL : E_FAIL;
#else
    RT_NOREF(aId, aProgress);
    return E_NOTIMPL;
#endif
}

HRESULT Host::createUSBDeviceFilter(const com::Utf8Str &aName,
                                    ComPtr<IHostUSBDeviceFilter> &aFilter)
{
#ifdef VBOX_WITH_USB

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<HostUSBDeviceFilter> filter;
    filter.createObject();
    HRESULT hrc = filter->init(this, Bstr(aName).raw());
    ComAssertComRCRet(hrc, hrc);
    hrc = filter.queryInterfaceTo(aFilter.asOutParam());
    AssertComRCReturn(hrc, hrc);
    return S_OK;
#else
    /* Note: The GUI depends on this method returning E_NOTIMPL with no
     * extended error info to indicate that USB is simply not available
     * (w/o treating it as a failure), for example, as in OSE. */
    RT_NOREF(aName, aFilter);
    ReturnComNotImplemented();
#endif
}

HRESULT Host::insertUSBDeviceFilter(ULONG aPosition,
                                    const ComPtr<IHostUSBDeviceFilter> &aFilter)
{
#ifdef VBOX_WITH_USB
    /* Note: HostUSBDeviceFilter and USBProxyService also uses this lock. */

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    MultiResult hrcMult = i_checkUSBProxyService();
    if (FAILED(hrcMult))
        return hrcMult;

    ComObjPtr<HostUSBDeviceFilter> pFilter;
    for (USBDeviceFilterList::iterator it = m->llChildren.begin();
         it != m->llChildren.end();
         ++it)
    {
        if (*it == aFilter)
        {
            pFilter = *it;
            break;
        }
    }
    if (pFilter.isNull())
        return setError(VBOX_E_INVALID_OBJECT_STATE,
                        tr("The given USB device filter is not created within this VirtualBox instance"));

    if (pFilter->mInList)
        return setError(E_INVALIDARG,
                        tr("The given USB device filter is already in the list"));

    /* iterate to the position... */
    USBDeviceFilterList::iterator itPos = m->llUSBDeviceFilters.begin();
    std::advance(itPos, aPosition);
    /* ...and insert */
    m->llUSBDeviceFilters.insert(itPos, pFilter);
    pFilter->mInList = true;

    /* notify the proxy (only when the filter is active) */
    if (    m->pUSBProxyService->isActive()
         && pFilter->i_getData().mData.fActive)
    {
        ComAssertRet(pFilter->i_getId() == NULL, E_FAIL);
        pFilter->i_getId() = m->pUSBProxyService->insertFilter(&pFilter->i_getData().mUSBFilter);
    }

    // save the global settings; for that we should hold only the VirtualBox lock
    alock.release();
    AutoWriteLock vboxLock(m->pParent COMMA_LOCKVAL_SRC_POS);
    return hrcMult = m->pParent->i_saveSettings();
#else

    /* Note: The GUI depends on this method returning E_NOTIMPL with no
     * extended error info to indicate that USB is simply not available
     * (w/o treating it as a failure), for example, as in OSE. */
    RT_NOREF(aPosition, aFilter);
    ReturnComNotImplemented();
#endif
}

HRESULT Host::removeUSBDeviceFilter(ULONG aPosition)
{
#ifdef VBOX_WITH_USB

    /* Note: HostUSBDeviceFilter and USBProxyService also uses this lock. */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    MultiResult hrcMult = i_checkUSBProxyService();
    if (FAILED(hrcMult))
        return hrcMult;

    if (!m->llUSBDeviceFilters.size())
        return setError(E_INVALIDARG,
                        tr("The USB device filter list is empty"));

    if (aPosition >= m->llUSBDeviceFilters.size())
        return setError(E_INVALIDARG,
                        tr("Invalid position: %lu (must be in range [0, %lu])"),
                        aPosition, m->llUSBDeviceFilters.size() - 1);

    ComObjPtr<HostUSBDeviceFilter> filter;
    {
        /* iterate to the position... */
        USBDeviceFilterList::iterator it = m->llUSBDeviceFilters.begin();
        std::advance(it, aPosition);
        /* ...get an element from there... */
        filter = *it;
        /* ...and remove */
        filter->mInList = false;
        m->llUSBDeviceFilters.erase(it);
    }

    /* notify the proxy (only when the filter is active) */
    if (m->pUSBProxyService->isActive() && filter->i_getData().mData.fActive)
    {
        ComAssertRet(filter->i_getId() != NULL, E_FAIL);
        m->pUSBProxyService->removeFilter(filter->i_getId());
        filter->i_getId() = NULL;
    }

    // save the global settings; for that we should hold only the VirtualBox lock
    alock.release();
    AutoWriteLock vboxLock(m->pParent COMMA_LOCKVAL_SRC_POS);
    return hrcMult = m->pParent->i_saveSettings();
#else
    /* Note: The GUI depends on this method returning E_NOTIMPL with no
     * extended error info to indicate that USB is simply not available
     * (w/o treating it as a failure), for example, as in OSE. */
    RT_NOREF(aPosition);
    ReturnComNotImplemented();
#endif
}

HRESULT Host::findHostDVDDrive(const com::Utf8Str &aName,
                               ComPtr<IMedium> &aDrive)
{
    ComObjPtr<Medium> medium;
    HRESULT hrc = i_findHostDriveByNameOrId(DeviceType_DVD, aName, medium);
    if (SUCCEEDED(hrc))
        hrc = medium.queryInterfaceTo(aDrive.asOutParam());
    else
        hrc = setError(hrc, tr("The host DVD drive named '%s' could not be found"), aName.c_str());
    return hrc;
}

HRESULT Host::findHostFloppyDrive(const com::Utf8Str &aName, ComPtr<IMedium> &aDrive)
{
    aDrive = NULL;

    ComObjPtr<Medium>medium;

    HRESULT hrc = i_findHostDriveByNameOrId(DeviceType_Floppy, aName, medium);
    if (SUCCEEDED(hrc))
        return medium.queryInterfaceTo(aDrive.asOutParam());
    return setError(hrc, tr("The host floppy drive named '%s' could not be found"), aName.c_str());
}

HRESULT Host::findHostNetworkInterfaceByName(const com::Utf8Str &aName,
                                             ComPtr<IHostNetworkInterface> &aNetworkInterface)
{
#ifndef VBOX_WITH_HOSTNETIF_API
    RT_NOREF(aName, aNetworkInterface);
    return E_NOTIMPL;
#else
    if (!aName.length())
        return E_INVALIDARG;

    HRESULT hrc = i_updateNetIfList();
    if (FAILED(hrc))
    {
        Log(("Failed to update host network interface list with hrc=%Rhrc\n", hrc));
        return hrc;
    }
#if defined(RT_OS_WINDOWS)
    hrc = i_updatePersistentConfigForHostOnlyAdapters();
    if (FAILED(hrc))
    {
        LogRel(("Failed to update persistent config for host-only adapters with hrc=%Rhrc\n", hrc));
        return hrc;
    }
#endif /* defined(RT_OS_WINDOWS) */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<HostNetworkInterface> found;
    for (HostNetworkInterfaceList::iterator it = m->llNetIfs.begin(); it != m->llNetIfs.end(); ++it)
    {
        Bstr n;
        (*it)->COMGETTER(Name)(n.asOutParam());
        if (n == aName)
            found = *it;
    }

    if (!found)
        return setError(E_INVALIDARG,
                        tr("The host network interface named '%s' could not be found"), aName.c_str());

    return found.queryInterfaceTo(aNetworkInterface.asOutParam());
#endif
}

HRESULT Host::findHostNetworkInterfaceById(const com::Guid &aId,
                                           ComPtr<IHostNetworkInterface> &aNetworkInterface)
{
#ifndef VBOX_WITH_HOSTNETIF_API
    RT_NOREF(aId, aNetworkInterface);
    return E_NOTIMPL;
#else
    if (!aId.isValid())
        return E_INVALIDARG;

    HRESULT hrc = i_updateNetIfList();
    if (FAILED(hrc))
    {
        Log(("Failed to update host network interface list with hrc=%Rhrc\n", hrc));
        return hrc;
    }
#if defined(RT_OS_WINDOWS)
    hrc = i_updatePersistentConfigForHostOnlyAdapters();
    if (FAILED(hrc))
    {
        LogRel(("Failed to update persistent config for host-only adapters with hrc=%Rhrc\n", hrc));
        return hrc;
    }
#endif /* defined(RT_OS_WINDOWS) */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    ComObjPtr<HostNetworkInterface> found;
    for (HostNetworkInterfaceList::iterator it = m->llNetIfs.begin(); it != m->llNetIfs.end(); ++it)
    {
        Bstr g;
        (*it)->COMGETTER(Id)(g.asOutParam());
        if (Guid(g) == aId)
            found = *it;
    }

    if (!found)
        return setError(E_INVALIDARG,
                        tr("The host network interface with the given GUID could not be found"));
    return found.queryInterfaceTo(aNetworkInterface.asOutParam());

#endif
}

HRESULT Host::findHostNetworkInterfacesOfType(HostNetworkInterfaceType_T aType,
                                              std::vector<ComPtr<IHostNetworkInterface> > &aNetworkInterfaces)
{
#ifdef VBOX_WITH_HOSTNETIF_API
    HRESULT hrc = i_updateNetIfList();
    if (FAILED(hrc))
    {
        Log(("Failed to update host network interface list with hrc=%Rhrc\n", hrc));
        return hrc;
    }
#if defined(RT_OS_WINDOWS)
    hrc = i_updatePersistentConfigForHostOnlyAdapters();
    if (FAILED(hrc))
    {
        LogRel(("Failed to update persistent config for host-only adapters with hrc=%Rhrc\n", hrc));
        return hrc;
    }
#endif /* defined(RT_OS_WINDOWS) */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HostNetworkInterfaceList resultList;
    for (HostNetworkInterfaceList::iterator it = m->llNetIfs.begin(); it != m->llNetIfs.end(); ++it)
    {
        HostNetworkInterfaceType_T t;
        hrc = (*it)->COMGETTER(InterfaceType)(&t);
        if (FAILED(hrc))
            return hrc;

        if (t == aType)
            resultList.push_back(*it);
    }
    aNetworkInterfaces.resize(resultList.size());
    size_t i = 0;
    for (HostNetworkInterfaceList::iterator it = resultList.begin(); it != resultList.end(); ++it, ++i)
    {
        (*it).queryInterfaceTo(aNetworkInterfaces[i].asOutParam());
    }

    return S_OK;
#else
    RT_NOREF(aType, aNetworkInterfaces);
    return E_NOTIMPL;
#endif
}

HRESULT Host::findUSBDeviceByAddress(const com::Utf8Str &aName,
                                     ComPtr<IHostUSBDevice> &aDevice)
{
#ifdef VBOX_WITH_USB

    aDevice = NULL;
    SafeIfaceArray<IHostUSBDevice> devsvec;
    HRESULT hrc = COMGETTER(USBDevices)(ComSafeArrayAsOutParam(devsvec));
    if (FAILED(hrc))
        return hrc;

    for (size_t i = 0; i < devsvec.size(); ++i)
    {
        Bstr address;
        hrc = devsvec[i]->COMGETTER(Address)(address.asOutParam());
        if (FAILED(hrc))
            return hrc;
        if (address == aName)
        {
            return (ComPtr<IHostUSBDevice>(devsvec[i]).queryInterfaceTo(aDevice.asOutParam()));
        }
    }

    return setErrorNoLog(VBOX_E_OBJECT_NOT_FOUND,
                         tr("Could not find a USB device with address '%s'"),
                         aName.c_str());

#else   /* !VBOX_WITH_USB */
    RT_NOREF(aName, aDevice);
    return E_NOTIMPL;
#endif  /* !VBOX_WITH_USB */
}
HRESULT Host::findUSBDeviceById(const com::Guid &aId,
                                ComPtr<IHostUSBDevice> &aDevice)
{
#ifdef VBOX_WITH_USB
    if (!aId.isValid())
        return E_INVALIDARG;

    aDevice = NULL;

    SafeIfaceArray<IHostUSBDevice> devsvec;
    HRESULT hrc = COMGETTER(USBDevices)(ComSafeArrayAsOutParam(devsvec));
    if (FAILED(hrc))
        return hrc;

    for (size_t i = 0; i < devsvec.size(); ++i)
    {
        Bstr id;
        hrc = devsvec[i]->COMGETTER(Id)(id.asOutParam());
        if (FAILED(hrc))
            return hrc;
        if (Guid(id) == aId)
        {
            return (ComPtr<IHostUSBDevice>(devsvec[i]).queryInterfaceTo(aDevice.asOutParam()));
        }
    }
    return setErrorNoLog(VBOX_E_OBJECT_NOT_FOUND,
                         tr("Could not find a USB device with uuid {%RTuuid}"),
                         aId.raw());

#else   /* !VBOX_WITH_USB */
    RT_NOREF(aId, aDevice);
    return E_NOTIMPL;
#endif  /* !VBOX_WITH_USB */
}

HRESULT Host::generateMACAddress(com::Utf8Str &aAddress)
{
    // no locking required
    i_generateMACAddress(aAddress);
    return S_OK;
}

/**
 * Returns a list of host video capture devices (webcams, etc).
 *
 * @returns COM status code
 * @param aVideoInputDevices Array of interface pointers to be filled.
 */
HRESULT Host::getVideoInputDevices(std::vector<ComPtr<IHostVideoInputDevice> > &aVideoInputDevices)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    HostVideoInputDeviceList list;

    HRESULT hrc = HostVideoInputDevice::queryHostDevices(m->pParent, &list);
    if (FAILED(hrc))
        return hrc;

    aVideoInputDevices.resize(list.size());
    size_t i = 0;
    for (HostVideoInputDeviceList::const_iterator it = list.begin(); it != list.end(); ++it, ++i)
        (*it).queryInterfaceTo(aVideoInputDevices[i].asOutParam());

    return S_OK;
}

/**
 * Returns the x86 host specific portions of the host object.
 *
 * @returns x86 host specific portions of the host object.
 * @param   aHostX86            Where to return the x86 host specific portions of the host objects.
 */
HRESULT Host::getX86(ComPtr<IHostX86> &aHostX86)
{
    return m->pHostX86.queryInterfaceTo(aHostX86.asOutParam());
}

HRESULT Host::addUSBDeviceSource(const com::Utf8Str &aBackend, const com::Utf8Str &aId, const com::Utf8Str &aAddress,
                                 const std::vector<com::Utf8Str> &aPropertyNames, const std::vector<com::Utf8Str> &aPropertyValues)
{
#ifdef VBOX_WITH_USB
    /* The USB proxy service will do the locking. */
    return m->pUSBProxyService->addUSBDeviceSource(aBackend, aId, aAddress, aPropertyNames, aPropertyValues);
#else
    RT_NOREF(aBackend, aId, aAddress, aPropertyNames, aPropertyValues);
    ReturnComNotImplemented();
#endif
}

HRESULT Host::removeUSBDeviceSource(const com::Utf8Str &aId)
{
#ifdef VBOX_WITH_USB
    /* The USB proxy service will do the locking. */
    return m->pUSBProxyService->removeUSBDeviceSource(aId);
#else
    RT_NOREF(aId);
    ReturnComNotImplemented();
#endif
}

HRESULT Host::getUpdateHost(ComPtr<IUpdateAgent> &aUpdate)
{
#ifdef VBOX_WITH_UPDATE_AGENT
    HRESULT hrc = m->pUpdateHost.queryInterfaceTo(aUpdate.asOutParam());
    return hrc;
#else
    RT_NOREF(aUpdate);
    ReturnComNotImplemented();
#endif
}

HRESULT Host::getUpdateExtPack(ComPtr<IUpdateAgent> &aUpdate)
{
    RT_NOREF(aUpdate);
    ReturnComNotImplemented();
}

HRESULT Host::getUpdateGuestAdditions(ComPtr<IUpdateAgent> &aUpdate)
{
    RT_NOREF(aUpdate);
    ReturnComNotImplemented();
}

HRESULT  Host::getHostDrives(std::vector<ComPtr<IHostDrive> > &aHostDrives)
{
    std::list<std::pair<com::Utf8Str, com::Utf8Str> > llDrivesPathsList;
    HRESULT hrc = i_getDrivesPathsList(llDrivesPathsList);
    if (SUCCEEDED(hrc))
    {
        for (std::list<std::pair<com::Utf8Str, com::Utf8Str> >::const_iterator it = llDrivesPathsList.begin();
             it != llDrivesPathsList.end();
             ++it)
        {
            ComObjPtr<HostDrive> pHostDrive;
            hrc = pHostDrive.createObject();
            if (SUCCEEDED(hrc))
                hrc = pHostDrive->initFromPathAndModel(it->first, it->second);
            if (FAILED(hrc))
                break;
            aHostDrives.push_back(pHostDrive);
        }
    }
    return hrc;
}


HRESULT Host::isExecutionEngineSupported(CPUArchitecture_T enmCpuArchitecture, VMExecutionEngine_T enmExecutionEngine, BOOL *pfIsSupported)
{
    *pfIsSupported = FALSE;

    /* No need to lock anything as this is constant. */
    switch (enmCpuArchitecture)
    {
        case CPUArchitecture_x86:
        case CPUArchitecture_AMD64:
        {
            switch (enmExecutionEngine)
            {
                case VMExecutionEngine_Default:
                case VMExecutionEngine_Interpreter:
                    *pfIsSupported = TRUE;
                    break;
                case VMExecutionEngine_Recompiler:
#ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER
                    *pfIsSupported = TRUE;
#endif
                    break;
#ifdef RT_ARCH_AMD64
# ifndef VBOX_WITH_DRIVERLESS_FORCED
                case VMExecutionEngine_HwVirt:
                    *pfIsSupported = TRUE; /** @todo Check whether our driver is actually accessible?. */
                    break;
# endif
# ifdef VBOX_WITH_NATIVE_NEM
                case VMExecutionEngine_NativeApi:
                    *pfIsSupported = i_HostIsNativeApiSupported();
                    break;
# endif
#endif
                default:
                    break;
            }
            break;
        }

        case CPUArchitecture_ARMv8_64:
        {
#ifdef VBOX_WITH_VIRT_ARMV8
            switch (enmExecutionEngine)
            {
#ifdef RT_ARCH_ARM64
# ifdef VBOX_WITH_NATIVE_NEM
                case VMExecutionEngine_NativeApi:
                    *pfIsSupported = i_HostIsNativeApiSupported();
                    break;
# endif
#endif
                default:
                    break;
            }
#endif
            break;
        }

        /* Not supported at all right now. */
        case CPUArchitecture_ARMv8_32:
            break;

        default:
            AssertFailed();
            break;
    }

    return S_OK;
}


// public methods only for internal purposes
////////////////////////////////////////////////////////////////////////////////

HRESULT Host::i_loadSettings(const settings::Host &data)
{
    HRESULT hrc = S_OK;
#ifdef VBOX_WITH_USB
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    for (settings::USBDeviceFiltersList::const_iterator it = data.llUSBDeviceFilters.begin();
         it != data.llUSBDeviceFilters.end();
         ++it)
    {
        const settings::USBDeviceFilter &f = *it;
        ComObjPtr<HostUSBDeviceFilter> pFilter;
        pFilter.createObject();
        hrc = pFilter->init(this, f);
        if (FAILED(hrc))
            break;

        m->llUSBDeviceFilters.push_back(pFilter);
        pFilter->mInList = true;

        /* notify the proxy (only when the filter is active) */
        if (pFilter->i_getData().mData.fActive)
        {
            HostUSBDeviceFilter *flt = pFilter; /* resolve ambiguity */
            flt->i_getId() = m->pUSBProxyService->insertFilter(&pFilter->i_getData().mUSBFilter);
        }
    }

    hrc = m->pUSBProxyService->i_loadSettings(data.llUSBDeviceSources);
#else
    RT_NOREF(data);
#endif /* VBOX_WITH_USB */

#ifdef VBOX_WITH_UPDATE_AGENT
    hrc = m->pUpdateHost->i_loadSettings(data.updateHost);
    ComAssertComRCRet(hrc, hrc);
    /** @todo Add handling for ExtPack and Guest Additions updates here later. See @bugref{7983}. */
#endif

    return hrc;
}

HRESULT Host::i_saveSettings(settings::Host &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc;

#ifdef VBOX_WITH_USB
    data.llUSBDeviceFilters.clear();
    data.llUSBDeviceSources.clear();

    for (USBDeviceFilterList::const_iterator it = m->llUSBDeviceFilters.begin();
         it != m->llUSBDeviceFilters.end();
         ++it)
    {
        ComObjPtr<HostUSBDeviceFilter> pFilter = *it;
        settings::USBDeviceFilter f;
        pFilter->i_saveSettings(f);
        data.llUSBDeviceFilters.push_back(f);
    }

    hrc = m->pUSBProxyService->i_saveSettings(data.llUSBDeviceSources);
    ComAssertComRCRet(hrc, hrc);
#else
    RT_NOREF(data);
#endif /* VBOX_WITH_USB */

#ifdef VBOX_WITH_UPDATE_AGENT
    hrc = m->pUpdateHost->i_saveSettings(data.updateHost);
    ComAssertComRCRet(hrc, hrc);
    /** @todo Add handling for ExtPack and Guest Additions updates here later. See @bugref{7983}. */
#endif

    return S_OK;
}

/**
 * Sets the given pointer to point to the static list of DVD or floppy
 * drives in the Host instance data, depending on the @a mediumType
 * parameter.
 *
 * This builds the list on the first call; it adds or removes host drives
 * that may have changed if fRefresh == true.
 *
 * The caller must hold the medium tree write lock before calling this.
 * To protect the list to which the caller's pointer points, the caller
 * must also hold that lock.
 *
 * @param mediumType Must be DeviceType_Floppy or DeviceType_DVD.
 * @param fRefresh Whether to refresh the host drives list even if this is not the first call.
 * @param pll Caller's pointer which gets set to the static list of host drives.
 * @param treeLock Reference to media tree lock, need to drop it temporarily.
 * @returns COM status code
 */
HRESULT Host::i_getDrives(DeviceType_T mediumType,
                          bool fRefresh,
                          MediaList *&pll,
                          AutoWriteLock &treeLock)
{
    HRESULT hrc = S_OK;
    Assert(m->pParent->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());

    MediaList llNew;
    MediaList *pllCached;
    bool *pfListBuilt = NULL;

    switch (mediumType)
    {
        case DeviceType_DVD:
            if (!m->fDVDDrivesListBuilt || fRefresh)
            {
                hrc = i_buildDVDDrivesList(llNew);
                if (FAILED(hrc))
                    return hrc;
                pfListBuilt = &m->fDVDDrivesListBuilt;
            }
            pllCached = &m->llDVDDrives;
        break;

        case DeviceType_Floppy:
            if (!m->fFloppyDrivesListBuilt || fRefresh)
            {
                hrc = i_buildFloppyDrivesList(llNew);
                if (FAILED(hrc))
                    return hrc;
                pfListBuilt = &m->fFloppyDrivesListBuilt;
            }
            pllCached = &m->llFloppyDrives;
        break;

        default:
            return E_INVALIDARG;
    }

    if (pfListBuilt)
    {
        // a list was built in llNew above:
        if (!*pfListBuilt)
        {
            // this was the first call (instance bool is still false): then just copy the whole list and return
            *pllCached = llNew;
            // and mark the instance data as "built"
            *pfListBuilt = true;
        }
        else
        {
            // list was built, and this was a subsequent call: then compare the old and the new lists

            // remove drives from the cached list which are no longer present
            for (MediaList::iterator itCached = pllCached->begin();
                 itCached != pllCached->end();
                 /*nothing */)
            {
                Medium *pCached = *itCached;
                const Utf8Str strLocationCached = pCached->i_getLocationFull();
                bool fFound = false;
                for (MediaList::iterator itNew = llNew.begin();
                     itNew != llNew.end();
                     ++itNew)
                {
                    Medium *pNew = *itNew;
                    const Utf8Str strLocationNew = pNew->i_getLocationFull();
                    if (strLocationNew == strLocationCached)
                    {
                        fFound = true;
                        break;
                    }
                }
                if (!fFound)
                {
                    pCached->uninit();
                    itCached = pllCached->erase(itCached);
                }
                else
                    ++itCached;
            }

            // add drives to the cached list that are not on there yet
            for (MediaList::iterator itNew = llNew.begin();
                 itNew != llNew.end();
                 ++itNew)
            {
                Medium *pNew = *itNew;
                const Utf8Str strLocationNew = pNew->i_getLocationFull();
                bool fFound = false;
                for (MediaList::iterator itCached = pllCached->begin();
                     itCached != pllCached->end();
                     ++itCached)
                {
                    Medium *pCached = *itCached;
                    const Utf8Str strLocationCached = pCached->i_getLocationFull();
                    if (strLocationNew == strLocationCached)
                    {
                        fFound = true;
                        break;
                    }
                }

                if (!fFound)
                    pllCached->push_back(pNew);
            }
        }
    }

    // return cached list to caller
    pll = pllCached;

    // Make sure the media tree lock is released before llNew is cleared,
    // as this usually triggers calls to uninit().
    treeLock.release();

    llNew.clear();

    treeLock.acquire();

    return hrc;
}

/**
 * Goes through the list of host drives that would be returned by getDrives()
 * and looks for a host drive with the given UUID. If found, it sets pMedium
 * to that drive; otherwise returns VBOX_E_OBJECT_NOT_FOUND.
 *
 * @param mediumType Must be DeviceType_DVD or DeviceType_Floppy.
 * @param uuid Medium UUID of host drive to look for.
 * @param fRefresh Whether to refresh the host drives list (see getDrives())
 * @param pMedium Medium object, if found...
 * @return VBOX_E_OBJECT_NOT_FOUND if not found, or S_OK if found, or errors from getDrives().
 */
HRESULT Host::i_findHostDriveById(DeviceType_T mediumType,
                                  const Guid &uuid,
                                  bool fRefresh,
                                  ComObjPtr<Medium> &pMedium)
{
    MediaList *pllMedia;

    AutoWriteLock treeLock(m->pParent->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = i_getDrives(mediumType, fRefresh, pllMedia, treeLock);
    if (SUCCEEDED(hrc))
    {
        for (MediaList::iterator it = pllMedia->begin();
             it != pllMedia->end();
             ++it)
        {
            Medium *pThis = *it;
            AutoCaller mediumCaller(pThis);
            AutoReadLock mediumLock(pThis COMMA_LOCKVAL_SRC_POS);
            if (pThis->i_getId() == uuid)
            {
                pMedium = pThis;
                return S_OK;
            }
        }
    }

    return VBOX_E_OBJECT_NOT_FOUND;
}

/**
 * Goes through the list of host drives that would be returned by getDrives()
 * and looks for a host drive with the given name. If found, it sets pMedium
 * to that drive; otherwise returns VBOX_E_OBJECT_NOT_FOUND.
 *
 * @param mediumType Must be DeviceType_DVD or DeviceType_Floppy.
 * @param strLocationFull Name (path) of host drive to look for.
 * @param fRefresh Whether to refresh the host drives list (see getDrives())
 * @param pMedium Medium object, if found
 * @return VBOX_E_OBJECT_NOT_FOUND if not found, or S_OK if found, or errors from getDrives().
 */
HRESULT Host::i_findHostDriveByName(DeviceType_T mediumType,
                                    const Utf8Str &strLocationFull,
                                    bool fRefresh,
                                    ComObjPtr<Medium> &pMedium)
{
    MediaList *pllMedia;

    AutoWriteLock treeLock(m->pParent->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc = i_getDrives(mediumType, fRefresh, pllMedia, treeLock);
    if (SUCCEEDED(hrc))
    {
        for (MediaList::iterator it = pllMedia->begin();
             it != pllMedia->end();
             ++it)
        {
            Medium *pThis = *it;
            AutoCaller mediumCaller(pThis);
            AutoReadLock mediumLock(pThis COMMA_LOCKVAL_SRC_POS);
            if (pThis->i_getLocationFull() == strLocationFull)
            {
                pMedium = pThis;
                return S_OK;
            }
        }
    }

    return VBOX_E_OBJECT_NOT_FOUND;
}

/**
 * Goes through the list of host drives that would be returned by getDrives()
 * and looks for a host drive with the given name, location or ID. If found,
 * it sets pMedium to that drive; otherwise returns VBOX_E_OBJECT_NOT_FOUND.
 *
 * @param mediumType  Must be DeviceType_DVD or DeviceType_Floppy.
 * @param strNameOrId Name or full location or UUID of host drive to look for.
 * @param pMedium     Medium object, if found...
 * @return VBOX_E_OBJECT_NOT_FOUND if not found, or S_OK if found, or errors from getDrives().
 */
HRESULT Host::i_findHostDriveByNameOrId(DeviceType_T mediumType,
                                        const Utf8Str &strNameOrId,
                                        ComObjPtr<Medium> &pMedium)
{
    AutoWriteLock wlock(m->pParent->i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    Guid uuid(strNameOrId);
    if (uuid.isValid() && !uuid.isZero())
        return i_findHostDriveById(mediumType, uuid, true /* fRefresh */, pMedium);

    // string is not a syntactically valid UUID: try a name then
    return i_findHostDriveByName(mediumType, strNameOrId, true /* fRefresh */, pMedium);
}

/**
 * Called from getDrives() to build the DVD drives list.
 * @param   list    Media list
 * @return
 */
HRESULT Host::i_buildDVDDrivesList(MediaList &list)
{
    HRESULT hrc = S_OK;

    Assert(m->pParent->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());

    try
    {
#if defined(RT_OS_WINDOWS)
        int sz = GetLogicalDriveStrings(0, NULL);
        TCHAR *hostDrives = new TCHAR[sz+1];
        GetLogicalDriveStrings(sz, hostDrives);
        wchar_t driveName[3] = { '?', ':', '\0' };
        TCHAR *p = hostDrives;
        do
        {
            if (GetDriveType(p) == DRIVE_CDROM)
            {
                driveName[0] = *p;
                ComObjPtr<Medium> hostDVDDriveObj;
                hostDVDDriveObj.createObject();
                hostDVDDriveObj->init(m->pParent, DeviceType_DVD, Bstr(driveName));
                list.push_back(hostDVDDriveObj);
            }
            p += _tcslen(p) + 1;
        }
        while (*p);
        delete[] hostDrives;

#elif defined(RT_OS_SOLARIS)
# ifdef VBOX_USE_LIBHAL
        if (!i_getDVDInfoFromHal(list))
# endif
        {
            i_getDVDInfoFromDevTree(list);
        }

#elif defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
        if (RT_SUCCESS(m->hostDrives.updateDVDs()))
            for (DriveInfoList::const_iterator it = m->hostDrives.DVDBegin();
                SUCCEEDED(hrc) && it != m->hostDrives.DVDEnd(); ++it)
            {
                ComObjPtr<Medium> hostDVDDriveObj;
                Utf8Str location(it->mDevice);
                Utf8Str description(it->mDescription);
                if (SUCCEEDED(hrc))
                    hrc = hostDVDDriveObj.createObject();
                if (SUCCEEDED(hrc))
                    hrc = hostDVDDriveObj->init(m->pParent, DeviceType_DVD, location, description);
                if (SUCCEEDED(hrc))
                    list.push_back(hostDVDDriveObj);
            }
#elif defined(RT_OS_DARWIN)
        PDARWINDVD cur = DarwinGetDVDDrives();
        while (cur)
        {
            ComObjPtr<Medium> hostDVDDriveObj;
            hostDVDDriveObj.createObject();
            hostDVDDriveObj->init(m->pParent, DeviceType_DVD, Bstr(cur->szName));
            list.push_back(hostDVDDriveObj);

            /* next */
            void *freeMe = cur;
            cur = cur->pNext;
            RTMemFree(freeMe);
        }
#else
    /* PORTME */
#endif
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }
    return hrc;
}

/**
 * Called from getDrives() to build the floppy drives list.
 * @param list
 * @return
 */
HRESULT Host::i_buildFloppyDrivesList(MediaList &list)
{
    HRESULT hrc = S_OK;

    Assert(m->pParent->i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());

    try
    {
#ifdef RT_OS_WINDOWS
        int sz = GetLogicalDriveStrings(0, NULL);
        TCHAR *hostDrives = new TCHAR[sz+1];
        GetLogicalDriveStrings(sz, hostDrives);
        wchar_t driveName[3] = { '?', ':', '\0' };
        TCHAR *p = hostDrives;
        do
        {
            if (GetDriveType(p) == DRIVE_REMOVABLE)
            {
                driveName[0] = *p;
                ComObjPtr<Medium> hostFloppyDriveObj;
                hostFloppyDriveObj.createObject();
                hostFloppyDriveObj->init(m->pParent, DeviceType_Floppy, Bstr(driveName));
                list.push_back(hostFloppyDriveObj);
            }
            p += _tcslen(p) + 1;
        }
        while (*p);
        delete[] hostDrives;
#elif defined(RT_OS_LINUX)
        if (RT_SUCCESS(m->hostDrives.updateFloppies()))
            for (DriveInfoList::const_iterator it = m->hostDrives.FloppyBegin();
                SUCCEEDED(hrc) && it != m->hostDrives.FloppyEnd(); ++it)
            {
                ComObjPtr<Medium> hostFloppyDriveObj;
                Utf8Str location(it->mDevice);
                Utf8Str description(it->mDescription);
                if (SUCCEEDED(hrc))
                    hrc = hostFloppyDriveObj.createObject();
                if (SUCCEEDED(hrc))
                    hrc = hostFloppyDriveObj->init(m->pParent, DeviceType_Floppy, location, description);
                if (SUCCEEDED(hrc))
                    list.push_back(hostFloppyDriveObj);
            }
#else
    RT_NOREF(list);
    /* PORTME */
#endif
    }
    catch(std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }

    return hrc;
}

#ifdef VBOX_WITH_USB
USBProxyService* Host::i_usbProxyService()
{
    return m->pUSBProxyService;
}

HRESULT Host::i_addChild(HostUSBDeviceFilter *pChild)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->llChildren.push_back(pChild);

    return S_OK;
}

HRESULT Host::i_removeChild(HostUSBDeviceFilter *pChild)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    for (USBDeviceFilterList::iterator it = m->llChildren.begin();
         it != m->llChildren.end();
         ++it)
    {
        if (*it == pChild)
        {
            m->llChildren.erase(it);
            break;
        }
    }

    return S_OK;
}

VirtualBox* Host::i_parent()
{
    return m->pParent;
}

/**
 *  Called by setter methods of all USB device filters.
 */
HRESULT Host::i_onUSBDeviceFilterChange(HostUSBDeviceFilter *aFilter,
                                        BOOL aActiveChanged /* = FALSE */)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aFilter->mInList)
    {
        if (aActiveChanged)
        {
            // insert/remove the filter from the proxy
            if (aFilter->i_getData().mData.fActive)
            {
                ComAssertRet(aFilter->i_getId() == NULL, E_FAIL);
                aFilter->i_getId() = m->pUSBProxyService->insertFilter(&aFilter->i_getData().mUSBFilter);
            }
            else
            {
                ComAssertRet(aFilter->i_getId() != NULL, E_FAIL);
                m->pUSBProxyService->removeFilter(aFilter->i_getId());
                aFilter->i_getId() = NULL;
            }
        }
        else
        {
            if (aFilter->i_getData().mData.fActive)
            {
                // update the filter in the proxy
                ComAssertRet(aFilter->i_getId() != NULL, E_FAIL);
                m->pUSBProxyService->removeFilter(aFilter->i_getId());
                aFilter->i_getId() = m->pUSBProxyService->insertFilter(&aFilter->i_getData().mUSBFilter);
            }
        }

        // save the global settings... yeah, on every single filter property change
        // for that we should hold only the VirtualBox lock
        alock.release();
        AutoWriteLock vboxLock(m->pParent COMMA_LOCKVAL_SRC_POS);
        return m->pParent->i_saveSettings();
    }

    return S_OK;
}


/**
 * Interface for obtaining a copy of the USBDeviceFilterList,
 * used by the USBProxyService.
 *
 * @param   aGlobalFilters      Where to put the global filter list copy.
 * @param   aMachines           Where to put the machine vector.
 */
void Host::i_getUSBFilters(Host::USBDeviceFilterList *aGlobalFilters)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aGlobalFilters = m->llUSBDeviceFilters;
}

#endif /* VBOX_WITH_USB */

// private methods
////////////////////////////////////////////////////////////////////////////////

#if defined(RT_OS_SOLARIS) && defined(VBOX_USE_LIBHAL)

/**
 * Helper function to get the slice number from a device path
 *
 * @param   pszDevLinkPath      Pointer to a device path (/dev/(r)dsk/c7d1t0d0s3 etc.)
 * @returns Pointer to the slice portion of the given path.
 */
static char *solarisGetSliceFromPath(const char *pszDevLinkPath)
{
    char *pszSlice = (char *)strrchr(pszDevLinkPath, 's');
    char *pszDisk  = (char *)strrchr(pszDevLinkPath, 'd');
    char *pszFound;
    if (pszSlice && (uintptr_t)pszSlice > (uintptr_t)pszDisk)
        pszFound = pszSlice;
    else
        pszFound = pszDisk;

    if (pszFound && RT_C_IS_DIGIT(pszFound[1]))
        return pszFound;

    return NULL;
}

/**
 * Walk device links and returns an allocated path for the first one in the snapshot.
 *
 * @param   DevLink     Handle to the device link being walked.
 * @param   pvArg       Opaque pointer that we use to point to the return
 *                      variable (char *).  Caller must call RTStrFree on it.
 * @returns DI_WALK_TERMINATE to stop the walk.
 */
static int solarisWalkDevLink(di_devlink_t DevLink, void *pvArg)
{
    char **ppszPath = (char **)pvArg;
    *ppszPath = RTStrDup(di_devlink_path(DevLink));
    return DI_WALK_TERMINATE;
}

/**
 * Walk all devices in the system and enumerate CD/DVD drives.
 * @param   Node        Handle to the current node.
 * @param   pvArg       Opaque data (holds list pointer).
 * @returns Solaris specific code whether to continue walking or not.
 */
static int solarisWalkDeviceNodeForDVD(di_node_t Node, void *pvArg)
{
    PSOLARISDVD *ppDrives = (PSOLARISDVD *)pvArg;

    /*
     * Check for "removable-media" or "hotpluggable" instead of "SCSI" so that we also include USB CD-ROMs.
     * As unfortunately the Solaris drivers only export these common properties.
     */
    int *pInt = NULL;
    if (   di_prop_lookup_ints(DDI_DEV_T_ANY, Node, "removable-media", &pInt) >= 0
        || di_prop_lookup_ints(DDI_DEV_T_ANY, Node, "hotpluggable", &pInt) >= 0)
    {
        if (di_prop_lookup_ints(DDI_DEV_T_ANY, Node, "inquiry-device-type", &pInt) > 0
            && (   *pInt == DTYPE_RODIRECT                                              /* CDROM */
                || *pInt == DTYPE_OPTICAL))                                             /* Optical Drive */
        {
            char *pszProduct = NULL;
            if (di_prop_lookup_strings(DDI_DEV_T_ANY, Node, "inquiry-product-id", &pszProduct) > 0)
            {
                char *pszVendor = NULL;
                if (di_prop_lookup_strings(DDI_DEV_T_ANY, Node, "inquiry-vendor-id", &pszVendor) > 0)
                {
                    /*
                     * Found a DVD drive, we need to scan the minor nodes to find the correct
                     * slice that represents the whole drive. "s2" is always the whole drive for CD/DVDs.
                     */
                    int Major = di_driver_major(Node);
                    di_minor_t Minor = DI_MINOR_NIL;
                    di_devlink_handle_t DevLink = di_devlink_init(NULL /* name */, 0 /* flags */);
                    if (DevLink)
                    {
                        while ((Minor = di_minor_next(Node, Minor)) != DI_MINOR_NIL)
                        {
                            dev_t Dev = di_minor_devt(Minor);
                            if (   Major != (int)major(Dev)
                                || di_minor_spectype(Minor) == S_IFBLK
                                || di_minor_type(Minor) != DDM_MINOR)
                            {
                                continue;
                            }

                            char *pszMinorPath = di_devfs_minor_path(Minor);
                            if (!pszMinorPath)
                                continue;

                            char *pszDevLinkPath = NULL;
                            di_devlink_walk(DevLink, NULL, pszMinorPath, DI_PRIMARY_LINK, &pszDevLinkPath, solarisWalkDevLink);
                            di_devfs_path_free(pszMinorPath);

                            if (pszDevLinkPath)
                            {
                                char *pszSlice = solarisGetSliceFromPath(pszDevLinkPath);
                                if (   pszSlice && !strcmp(pszSlice, "s2")
                                    && !strncmp(pszDevLinkPath, RT_STR_TUPLE("/dev/rdsk")))   /* We want only raw disks */
                                {
                                    /*
                                     * We've got a fully qualified DVD drive. Add it to the list.
                                     */
                                    PSOLARISDVD pDrive = (PSOLARISDVD)RTMemAllocZ(sizeof(SOLARISDVD));
                                    if (RT_LIKELY(pDrive))
                                    {
                                        RTStrPrintf(pDrive->szDescription, sizeof(pDrive->szDescription),
                                                    "%s %s", pszVendor, pszProduct);
                                        RTStrPurgeEncoding(pDrive->szDescription);
                                        RTStrCopy(pDrive->szRawDiskPath, sizeof(pDrive->szRawDiskPath), pszDevLinkPath);
                                        if (*ppDrives)
                                            pDrive->pNext = *ppDrives;
                                        *ppDrives = pDrive;

                                        /* We're not interested in any of the other slices, stop minor nodes traversal. */
                                        RTStrFree(pszDevLinkPath);
                                        break;
                                    }
                                }
                                RTStrFree(pszDevLinkPath);
                            }
                        }
                        di_devlink_fini(&DevLink);
                    }
                }
            }
        }
    }
    return DI_WALK_CONTINUE;
}

/**
 * Solaris specific function to enumerate CD/DVD drives via the device tree.
 * Works on Solaris 10 as well as OpenSolaris without depending on libhal.
 */
void Host::i_getDVDInfoFromDevTree(std::list<ComObjPtr<Medium> > &list)
{
    PSOLARISDVD pDrives = NULL;
    di_node_t RootNode = di_init("/", DINFOCPYALL);
    if (RootNode != DI_NODE_NIL)
        di_walk_node(RootNode, DI_WALK_CLDFIRST, &pDrives, solarisWalkDeviceNodeForDVD);

    di_fini(RootNode);

    while (pDrives)
    {
        ComObjPtr<Medium> hostDVDDriveObj;
        hostDVDDriveObj.createObject();
        hostDVDDriveObj->init(m->pParent, DeviceType_DVD, Bstr(pDrives->szRawDiskPath), Bstr(pDrives->szDescription));
        list.push_back(hostDVDDriveObj);

        void *pvDrive = pDrives;
        pDrives = pDrives->pNext;
        RTMemFree(pvDrive);
    }
}


/**
 * Walk all devices in the system and enumerate fixed drives.
 * @param   Node        Handle to the current node.
 * @param   pvArg       Opaque data (holds list pointer).
 * @returns Solaris specific code whether to continue walking or not.
 */
static int solarisWalkDeviceNodeForFixedDrive(di_node_t Node, void *pvArg) RT_NOEXCEPT
{
    PSOLARISFIXEDDISK *ppDrives = (PSOLARISFIXEDDISK *)pvArg;

    int *pInt = NULL;
    if (    di_prop_lookup_ints(DDI_DEV_T_ANY, Node, "inquiry-device-type", &pInt) > 0
        && *pInt == DTYPE_DIRECT) /* Fixed drive */
    {
        char *pszProduct = NULL;
        if (di_prop_lookup_strings(DDI_DEV_T_ANY, Node, "inquiry-product-id", &pszProduct) > 0)
        {
            char *pszVendor = NULL;
            if (di_prop_lookup_strings(DDI_DEV_T_ANY, Node, "inquiry-vendor-id", &pszVendor) > 0)
            {
                /*
                 * Found a fixed drive, we need to scan the minor nodes to find the correct
                 * slice that represents the whole drive.
                 */
                int Major = di_driver_major(Node);
                di_minor_t Minor = DI_MINOR_NIL;
                di_devlink_handle_t DevLink = di_devlink_init(NULL /* name */, 0 /* flags */);
                if (DevLink)
                {
                    /*
                     * The device name we have to select depends on drive type. For fixed drives, the
                     * name without slice or partition should be selected, for USB flash drive the
                     * partition 0 should be selected and slice 0 for other cases.
                     */
                    char *pszDisk       = NULL;
                    char *pszPartition0 = NULL;
                    char *pszSlice0     = NULL;
                    while ((Minor = di_minor_next(Node, Minor)) != DI_MINOR_NIL)
                    {
                        dev_t Dev = di_minor_devt(Minor);
                        if (   Major != (int)major(Dev)
                            || di_minor_spectype(Minor) == S_IFBLK
                            || di_minor_type(Minor) != DDM_MINOR)
                            continue;

                        char *pszMinorPath = di_devfs_minor_path(Minor);
                        if (!pszMinorPath)
                            continue;

                        char *pszDevLinkPath = NULL;
                        di_devlink_walk(DevLink, NULL, pszMinorPath, DI_PRIMARY_LINK, &pszDevLinkPath, solarisWalkDevLink);
                        di_devfs_path_free(pszMinorPath);

                        if (pszDevLinkPath)
                        {
                            char const *pszCurSlice = strrchr(pszDevLinkPath, 's');
                            char const *pszCurDisk  = strrchr(pszDevLinkPath, 'd');
                            char const *pszCurPart  = strrchr(pszDevLinkPath, 'p');
                            char **ppszDst  = NULL;
                            if (pszCurSlice && (uintptr_t)pszCurSlice > (uintptr_t)pszCurDisk && !strcmp(pszCurSlice, "s0"))
                                ppszDst = &pszSlice0;
                            else if (pszCurPart && (uintptr_t)pszCurPart > (uintptr_t)pszCurDisk && !strcmp(pszCurPart, "p0"))
                                ppszDst = &pszPartition0;
                            else if (   (!pszCurSlice || (uintptr_t)pszCurSlice < (uintptr_t)pszCurDisk)
                                     && (!pszCurPart  || (uintptr_t)pszCurPart  < (uintptr_t)pszCurDisk)
                                     && *pszDevLinkPath != '\0')
                                ppszDst = &pszDisk;
                            else
                                RTStrFree(pszDevLinkPath);
                            if (ppszDst)
                            {
                                if (*ppszDst != NULL)
                                    RTStrFree(*ppszDst);
                                *ppszDst = pszDevLinkPath;
                            }
                        }
                    }
                    di_devlink_fini(&DevLink);
                    if (pszDisk || pszPartition0 || pszSlice0)
                    {
                        PSOLARISFIXEDDISK pDrive = (PSOLARISFIXEDDISK)RTMemAllocZ(sizeof(*pDrive));
                        if (RT_LIKELY(pDrive))
                        {
                            RTStrPrintf(pDrive->szDescription, sizeof(pDrive->szDescription), "%s %s", pszVendor, pszProduct);
                            RTStrPurgeEncoding(pDrive->szDescription);

                            const char *pszDevPath = pszDisk ? pszDisk : pszPartition0 ? pszPartition0 : pszSlice0;
                            int vrc = RTStrCopy(pDrive->szRawDiskPath, sizeof(pDrive->szRawDiskPath), pszDevPath);
                            AssertRC(vrc);

                            if (*ppDrives)
                                pDrive->pNext = *ppDrives;
                            *ppDrives = pDrive;
                        }
                        RTStrFree(pszDisk);
                        RTStrFree(pszPartition0);
                        RTStrFree(pszSlice0);
                    }
                }
            }
        }
    }
    return DI_WALK_CONTINUE;
}


/**
 * Solaris specific function to enumerate fixed drives via the device tree.
 * Works on Solaris 10 as well as OpenSolaris without depending on libhal.
 *
 * @returns COM status, either S_OK or E_OUTOFMEMORY.
 * @param   list        Reference to list where the the path/model pairs are to
 *                      be returned.
 */
HRESULT Host::i_getFixedDrivesFromDevTree(std::list<std::pair<com::Utf8Str, com::Utf8Str> > &list) RT_NOEXCEPT
{
    PSOLARISFIXEDDISK pDrives = NULL;
    di_node_t RootNode = di_init("/", DINFOCPYALL);
    if (RootNode != DI_NODE_NIL)
        di_walk_node(RootNode, DI_WALK_CLDFIRST, &pDrives, solarisWalkDeviceNodeForFixedDrive);
    di_fini(RootNode);

    HRESULT hrc = S_OK;
    try
    {
        for (PSOLARISFIXEDDISK pCurDrv = pDrives; pCurDrv; pCurDrv = pCurDrv->pNext)
            list.push_back(std::pair<com::Utf8Str, com::Utf8Str>(pCurDrv->szRawDiskPath, pCurDrv->szDescription));
    }
    catch (std::bad_alloc &)
    {
        LogRelFunc(("Out of memory!\n"));
        list.clear();
        hrc = E_OUTOFMEMORY;
    }

    while (pDrives)
    {
        PSOLARISFIXEDDISK pFreeMe = pDrives;
        pDrives = pDrives->pNext;
        ASMCompilerBarrier();
        RTMemFree(pFreeMe);
    }

    return hrc;
}


/* Solaris hosts, loading libhal at runtime */

/**
 * Helper function to query the hal subsystem for information about DVD drives attached to the
 * system.
 *
 * @returns true if information was successfully obtained, false otherwise
 * @param   list        Reference to list where the DVDs drives are to be returned.
 */
bool Host::i_getDVDInfoFromHal(std::list<ComObjPtr<Medium> > &list)
{
    bool halSuccess = false;
    DBusError dbusError;
    if (!gLibHalCheckPresence())
        return false;
    gDBusErrorInit(&dbusError);
    DBusConnection *dbusConnection = gDBusBusGet(DBUS_BUS_SYSTEM, &dbusError);
    if (dbusConnection != 0)
    {
        LibHalContext *halContext = gLibHalCtxNew();
        if (halContext != 0)
        {
            if (gLibHalCtxSetDBusConnection(halContext, dbusConnection))
            {
                if (gLibHalCtxInit(halContext, &dbusError))
                {
                    int numDevices;
                    char **halDevices = gLibHalFindDeviceStringMatch(halContext,
                                                "storage.drive_type", "cdrom",
                                                &numDevices, &dbusError);
                    if (halDevices != 0)
                    {
                        /* Hal is installed and working, so if no devices are reported, assume
                           that there are none. */
                        halSuccess = true;
                        for (int i = 0; i < numDevices; i++)
                        {
                            char *devNode = gLibHalDeviceGetPropertyString(halContext,
                                                    halDevices[i], "block.device", &dbusError);
#ifdef RT_OS_SOLARIS
                            /* The CD/DVD ioctls work only for raw device nodes. */
                            char *tmp = getfullrawname(devNode);
                            gLibHalFreeString(devNode);
                            devNode = tmp;
#endif

                            if (devNode != 0)
                            {
//                                if (validateDevice(devNode, true))
//                                {
                                    Utf8Str description;
                                    char *vendor, *product;
                                    /* We do not check the error here, as this field may
                                       not even exist. */
                                    vendor = gLibHalDeviceGetPropertyString(halContext,
                                                    halDevices[i], "info.vendor", 0);
                                    product = gLibHalDeviceGetPropertyString(halContext,
                                                    halDevices[i], "info.product", &dbusError);
                                    if ((product != 0 && product[0] != 0))
                                    {
                                        if ((vendor != 0) && (vendor[0] != 0))
                                        {
                                            description = Utf8StrFmt("%s %s",
                                                                     vendor, product);
                                        }
                                        else
                                        {
                                            description = product;
                                        }
                                        ComObjPtr<Medium> hostDVDDriveObj;
                                        hostDVDDriveObj.createObject();
                                        hostDVDDriveObj->init(m->pParent, DeviceType_DVD,
                                                              Bstr(devNode), Bstr(description));
                                        list.push_back(hostDVDDriveObj);
                                    }
                                    else
                                    {
                                        if (product == 0)
                                        {
                                            LogRel(("Host::COMGETTER(DVDDrives): failed to get property \"info.product\" for device %s.  dbus error: %s (%s)\n",
                                                    halDevices[i], dbusError.name, dbusError.message));
                                            gDBusErrorFree(&dbusError);
                                        }
                                        ComObjPtr<Medium> hostDVDDriveObj;
                                        hostDVDDriveObj.createObject();
                                        hostDVDDriveObj->init(m->pParent, DeviceType_DVD,
                                                              Bstr(devNode));
                                        list.push_back(hostDVDDriveObj);
                                    }
                                    if (vendor != 0)
                                    {
                                        gLibHalFreeString(vendor);
                                    }
                                    if (product != 0)
                                    {
                                        gLibHalFreeString(product);
                                    }
//                                }
//                                else
//                                {
//                                    LogRel(("Host::COMGETTER(DVDDrives): failed to validate the block device %s as a DVD drive\n"));
//                                }
#ifndef RT_OS_SOLARIS
                                gLibHalFreeString(devNode);
#else
                                free(devNode);
#endif
                            }
                            else
                            {
                                LogRel(("Host::COMGETTER(DVDDrives): failed to get property \"block.device\" for device %s.  dbus error: %s (%s)\n",
                                        halDevices[i], dbusError.name, dbusError.message));
                                gDBusErrorFree(&dbusError);
                            }
                        }
                        gLibHalFreeStringArray(halDevices);
                    }
                    else
                    {
                        LogRel(("Host::COMGETTER(DVDDrives): failed to get devices with capability \"storage.cdrom\".  dbus error: %s (%s)\n", dbusError.name, dbusError.message));
                        gDBusErrorFree(&dbusError);
                    }
                    if (!gLibHalCtxShutdown(halContext, &dbusError))  /* what now? */
                    {
                        LogRel(("Host::COMGETTER(DVDDrives): failed to shutdown the libhal context.  dbus error: %s (%s)\n",
                                dbusError.name, dbusError.message));
                        gDBusErrorFree(&dbusError);
                    }
                }
                else
                {
                    LogRel(("Host::COMGETTER(DVDDrives): failed to initialise libhal context.  dbus error: %s (%s)\n",
                            dbusError.name, dbusError.message));
                    gDBusErrorFree(&dbusError);
                }
                gLibHalCtxFree(halContext);
            }
            else
            {
                LogRel(("Host::COMGETTER(DVDDrives): failed to set libhal connection to dbus.\n"));
            }
        }
        else
        {
            LogRel(("Host::COMGETTER(DVDDrives): failed to get a libhal context - out of memory?\n"));
        }
        gDBusConnectionUnref(dbusConnection);
    }
    else
    {
        LogRel(("Host::COMGETTER(DVDDrives): failed to connect to dbus.  dbus error: %s (%s)\n",
                dbusError.name, dbusError.message));
        gDBusErrorFree(&dbusError);
    }
    return halSuccess;
}


/**
 * Helper function to query the hal subsystem for information about floppy drives attached to the
 * system.
 *
 * @returns true if information was successfully obtained, false otherwise
 * @retval  list drives found will be attached to this list
 */
bool Host::i_getFloppyInfoFromHal(std::list< ComObjPtr<Medium> > &list)
{
    bool halSuccess = false;
    DBusError dbusError;
    if (!gLibHalCheckPresence())
        return false;
    gDBusErrorInit(&dbusError);
    DBusConnection *dbusConnection = gDBusBusGet(DBUS_BUS_SYSTEM, &dbusError);
    if (dbusConnection != 0)
    {
        LibHalContext *halContext = gLibHalCtxNew();
        if (halContext != 0)
        {
            if (gLibHalCtxSetDBusConnection(halContext, dbusConnection))
            {
                if (gLibHalCtxInit(halContext, &dbusError))
                {
                    int numDevices;
                    char **halDevices = gLibHalFindDeviceStringMatch(halContext,
                                                "storage.drive_type", "floppy",
                                                &numDevices, &dbusError);
                    if (halDevices != 0)
                    {
                        /* Hal is installed and working, so if no devices are reported, assume
                           that there are none. */
                        halSuccess = true;
                        for (int i = 0; i < numDevices; i++)
                        {
                            char *driveType = gLibHalDeviceGetPropertyString(halContext,
                                                    halDevices[i], "storage.drive_type", 0);
                            if (driveType != 0)
                            {
                                if (strcmp(driveType, "floppy") != 0)
                                {
                                    gLibHalFreeString(driveType);
                                    continue;
                                }
                                gLibHalFreeString(driveType);
                            }
                            else
                            {
                                /* An error occurred.  The attribute "storage.drive_type"
                                   probably didn't exist. */
                                continue;
                            }
                            char *devNode = gLibHalDeviceGetPropertyString(halContext,
                                                    halDevices[i], "block.device", &dbusError);
                            if (devNode != 0)
                            {
//                                if (validateDevice(devNode, false))
//                                {
                                    Utf8Str description;
                                    char *vendor, *product;
                                    /* We do not check the error here, as this field may
                                       not even exist. */
                                    vendor = gLibHalDeviceGetPropertyString(halContext,
                                                    halDevices[i], "info.vendor", 0);
                                    product = gLibHalDeviceGetPropertyString(halContext,
                                                    halDevices[i], "info.product", &dbusError);
                                    if ((product != 0) && (product[0] != 0))
                                    {
                                        if ((vendor != 0) && (vendor[0] != 0))
                                        {
                                            description = Utf8StrFmt("%s %s",
                                                                     vendor, product);
                                        }
                                        else
                                        {
                                            description = product;
                                        }
                                        ComObjPtr<Medium> hostFloppyDrive;
                                        hostFloppyDrive.createObject();
                                        hostFloppyDrive->init(m->pParent, DeviceType_DVD,
                                                              Bstr(devNode), Bstr(description));
                                        list.push_back(hostFloppyDrive);
                                    }
                                    else
                                    {
                                        if (product == 0)
                                        {
                                            LogRel(("Host::COMGETTER(FloppyDrives): failed to get property \"info.product\" for device %s.  dbus error: %s (%s)\n",
                                                    halDevices[i], dbusError.name, dbusError.message));
                                            gDBusErrorFree(&dbusError);
                                        }
                                        ComObjPtr<Medium> hostFloppyDrive;
                                        hostFloppyDrive.createObject();
                                        hostFloppyDrive->init(m->pParent, DeviceType_DVD,
                                                              Bstr(devNode));
                                        list.push_back(hostFloppyDrive);
                                    }
                                    if (vendor != 0)
                                    {
                                        gLibHalFreeString(vendor);
                                    }
                                    if (product != 0)
                                    {
                                        gLibHalFreeString(product);
                                    }
//                                }
//                                else
//                                {
//                                    LogRel(("Host::COMGETTER(FloppyDrives): failed to validate the block device %s as a floppy drive\n"));
//                                }
                                gLibHalFreeString(devNode);
                            }
                            else
                            {
                                LogRel(("Host::COMGETTER(FloppyDrives): failed to get property \"block.device\" for device %s.  dbus error: %s (%s)\n",
                                        halDevices[i], dbusError.name, dbusError.message));
                                gDBusErrorFree(&dbusError);
                            }
                        }
                        gLibHalFreeStringArray(halDevices);
                    }
                    else
                    {
                        LogRel(("Host::COMGETTER(FloppyDrives): failed to get devices with capability \"storage.cdrom\".  dbus error: %s (%s)\n", dbusError.name, dbusError.message));
                        gDBusErrorFree(&dbusError);
                    }
                    if (!gLibHalCtxShutdown(halContext, &dbusError))  /* what now? */
                    {
                        LogRel(("Host::COMGETTER(FloppyDrives): failed to shutdown the libhal context.  dbus error: %s (%s)\n",
                                dbusError.name, dbusError.message));
                        gDBusErrorFree(&dbusError);
                    }
                }
                else
                {
                    LogRel(("Host::COMGETTER(FloppyDrives): failed to initialise libhal context.  dbus error: %s (%s)\n",
                            dbusError.name, dbusError.message));
                    gDBusErrorFree(&dbusError);
                }
                gLibHalCtxFree(halContext);
            }
            else
            {
                LogRel(("Host::COMGETTER(FloppyDrives): failed to set libhal connection to dbus.\n"));
            }
        }
        else
        {
            LogRel(("Host::COMGETTER(FloppyDrives): failed to get a libhal context - out of memory?\n"));
        }
        gDBusConnectionUnref(dbusConnection);
    }
    else
    {
        LogRel(("Host::COMGETTER(FloppyDrives): failed to connect to dbus.  dbus error: %s (%s)\n",
                dbusError.name, dbusError.message));
        gDBusErrorFree(&dbusError);
    }
    return halSuccess;
}


/**
 * Helper function to query the hal subsystem for information about fixed drives attached to the
 * system.
 *
 * @returns COM status code. (setError is not called on failure as we only fail
 *          with E_OUTOFMEMORY.)
 * @retval  S_OK on success.
 * @retval  S_FALSE if HAL cannot be used.
 * @param   list        Reference to list to return the path/model string pairs.
 */
HRESULT Host::i_getFixedDrivesFromHal(std::list<std::pair<com::Utf8Str, com::Utf8Str> > &list) RT_NOEXCEPT
{
    HRESULT hrc = S_FALSE;
    if (!gLibHalCheckPresence())
        return hrc;

    DBusError dbusError;
    gDBusErrorInit(&dbusError);
    DBusConnection *dbusConnection = gDBusBusGet(DBUS_BUS_SYSTEM, &dbusError);
    if (dbusConnection != 0)
    {
        LibHalContext *halContext = gLibHalCtxNew();
        if (halContext != 0)
        {
            if (gLibHalCtxSetDBusConnection(halContext, dbusConnection))
            {
                if (gLibHalCtxInit(halContext, &dbusError))
                {
                    int cDevices;
                    char **halDevices = gLibHalFindDeviceStringMatch(halContext, "storage.drive_type", "disk",
                                                                     &cDevices, &dbusError);
                    if (halDevices != 0)
                    {
                        /* Hal is installed and working, so if no devices are reported, assume
                           that there are none. */
                        hrc = S_OK;
                        for (int i = 0; i < cDevices && hrc == S_OK; i++)
                        {
                            char *pszDevNode = gLibHalDeviceGetPropertyString(halContext, halDevices[i], "block.device",
                                                                              &dbusError);
                            /* The fixed drive ioctls work only for raw device nodes. */
                            char *pszTmp = getfullrawname(pszDevNode);
                            gLibHalFreeString(pszDevNode);
                            pszDevNode = pszTmp;
                            if (pszDevNode != 0)
                            {
                                /* We do not check the error here, as this field may
                                   not even exist. */
                                char *pszVendor = gLibHalDeviceGetPropertyString(halContext, halDevices[i], "info.vendor", 0);
                                char *pszProduct = gLibHalDeviceGetPropertyString(halContext, halDevices[i], "info.product",
                                                                                  &dbusError);
                                Utf8Str strDescription;
                                if (pszProduct != NULL && pszProduct[0] != '\0')
                                {
                                    int vrc;
                                    if (pszVendor != NULL && pszVendor[0] != '\0')
                                        vrc = strDescription.printfNoThrow("%s %s", pszVendor, pszProduct);
                                    else
                                        vrc = strDescription.assignNoThrow(pszProduct);
                                    AssertRCStmt(vrc, hrc = E_OUTOFMEMORY);
                                }
                                if (pszVendor != NULL)
                                    gLibHalFreeString(pszVendor);
                                if (pszProduct != NULL)
                                    gLibHalFreeString(pszProduct);

                                /* Correct device/partition/slice already choosen. Just add it to the return list */
                                if (hrc == S_OK)
                                    try
                                    {
                                        list.push_back(std::pair<com::Utf8Str, com::Utf8Str>(pszDevNode, strDescription));
                                    }
                                    catch (std::bad_alloc &)
                                    {
                                        AssertFailedStmt(hrc = E_OUTOFMEMORY);
                                    }
                                gLibHalFreeString(pszDevNode);
                            }
                            else
                            {
                                LogRel(("Host::COMGETTER(HostDrives): failed to get property \"block.device\" for device %s.  dbus error: %s (%s)\n",
                                        halDevices[i], dbusError.name, dbusError.message));
                                gDBusErrorFree(&dbusError);
                            }
                        }
                        gLibHalFreeStringArray(halDevices);
                    }
                    else
                    {
                        LogRel(("Host::COMGETTER(HostDrives): failed to get devices with capability \"storage.disk\".  dbus error: %s (%s)\n", dbusError.name, dbusError.message));
                        gDBusErrorFree(&dbusError);
                    }
                    if (!gLibHalCtxShutdown(halContext, &dbusError))  /* what now? */
                    {
                        LogRel(("Host::COMGETTER(HostDrives): failed to shutdown the libhal context.  dbus error: %s (%s)\n",
                                dbusError.name, dbusError.message));
                        gDBusErrorFree(&dbusError);
                    }
                }
                else
                {
                    LogRel(("Host::COMGETTER(HostDrives): failed to initialise libhal context.  dbus error: %s (%s)\n",
                            dbusError.name, dbusError.message));
                    gDBusErrorFree(&dbusError);
                }
                gLibHalCtxFree(halContext);
            }
            else
                LogRel(("Host::COMGETTER(HostDrives): failed to set libhal connection to dbus.\n"));
        }
        else
            LogRel(("Host::COMGETTER(HostDrives): failed to get a libhal context - out of memory?\n"));
        gDBusConnectionUnref(dbusConnection);
    }
    else
    {
        LogRel(("Host::COMGETTER(HostDrives): failed to connect to dbus.  dbus error: %s (%s)\n",
                dbusError.name, dbusError.message));
        gDBusErrorFree(&dbusError);
    }
    return hrc;
}

#endif  /* RT_OS_SOLARIS and VBOX_USE_HAL */

/** @todo get rid of dead code below - RT_OS_SOLARIS and RT_OS_LINUX are never both set */
#if defined(RT_OS_SOLARIS)

/**
 * Helper function to parse the given mount file and add found entries
 */
void Host::i_parseMountTable(char *mountTable, std::list< ComObjPtr<Medium> > &list)
{
#ifdef RT_OS_LINUX
    FILE *mtab = setmntent(mountTable, "r");
    if (mtab)
    {
        struct mntent *mntent;
        char *mnt_type;
        char *mnt_dev;
        char *tmp;
        while ((mntent = getmntent(mtab)))
        {
            mnt_type = (char*)malloc(strlen(mntent->mnt_type) + 1);
            mnt_dev = (char*)malloc(strlen(mntent->mnt_fsname) + 1);
            strcpy(mnt_type, mntent->mnt_type);
            strcpy(mnt_dev, mntent->mnt_fsname);
            // supermount fs case
            if (strcmp(mnt_type, "supermount") == 0)
            {
                tmp = strstr(mntent->mnt_opts, "fs=");
                if (tmp)
                {
                    free(mnt_type);
                    mnt_type = strdup(tmp + strlen("fs="));
                    if (mnt_type)
                    {
                        tmp = strchr(mnt_type, ',');
                        if (tmp)
                            *tmp = '\0';
                    }
                }
                tmp = strstr(mntent->mnt_opts, "dev=");
                if (tmp)
                {
                    free(mnt_dev);
                    mnt_dev = strdup(tmp + strlen("dev="));
                    if (mnt_dev)
                    {
                        tmp = strchr(mnt_dev, ',');
                        if (tmp)
                            *tmp = '\0';
                    }
                }
            }
            // use strstr here to cover things fs types like "udf,iso9660"
            if (strstr(mnt_type, "iso9660") == 0)
            {
                /** @todo check whether we've already got the drive in our list! */
                if (i_validateDevice(mnt_dev, true))
                {
                    ComObjPtr<Medium> hostDVDDriveObj;
                    hostDVDDriveObj.createObject();
                    hostDVDDriveObj->init(m->pParent, DeviceType_DVD, Bstr(mnt_dev));
                    list.push_back (hostDVDDriveObj);
                }
            }
            free(mnt_dev);
            free(mnt_type);
        }
        endmntent(mtab);
    }
#else  // RT_OS_SOLARIS
    FILE *mntFile = fopen(mountTable, "r");
    if (mntFile)
    {
        struct mnttab mntTab;
        while (getmntent(mntFile, &mntTab) == 0)
        {
            const char *mountName = mntTab.mnt_special;
            const char *mountPoint = mntTab.mnt_mountp;
            const char *mountFSType = mntTab.mnt_fstype;
            if (mountName && mountPoint && mountFSType)
            {
                // skip devices we are not interested in
                if ((*mountName && mountName[0] == '/') &&                      // skip 'fake' devices (like -hosts,
                                                                                // proc, fd, swap)
                    (*mountFSType && (strncmp(mountFSType, RT_STR_TUPLE("devfs")) != 0 &&  // skip devfs
                                                                                           // (i.e. /devices)
                                      strncmp(mountFSType, RT_STR_TUPLE("dev")) != 0 &&    // skip dev (i.e. /dev)
                                      strncmp(mountFSType, RT_STR_TUPLE("lofs")) != 0)))   // skip loop-back file-system (lofs)
                {
                    char *rawDevName = getfullrawname((char *)mountName);
                    if (i_validateDevice(rawDevName, true))
                    {
                        ComObjPtr<Medium> hostDVDDriveObj;
                        hostDVDDriveObj.createObject();
                        hostDVDDriveObj->init(m->pParent, DeviceType_DVD, Bstr(rawDevName));
                        list.push_back(hostDVDDriveObj);
                    }
                    free(rawDevName);
                }
            }
        }

        fclose(mntFile);
    }
#endif
}

/**
 * Helper function to check whether the given device node is a valid drive
 */
bool Host::i_validateDevice(const char *deviceNode, bool isCDROM)
{
    struct stat statInfo;
    bool retValue = false;

    // sanity check
    if (!deviceNode)
    {
        return false;
    }

    // first a simple stat() call
    if (stat(deviceNode, &statInfo) < 0)
    {
        return false;
    }
    else
    {
        if (isCDROM)
        {
            if (S_ISCHR(statInfo.st_mode) || S_ISBLK(statInfo.st_mode))
            {
                int fileHandle;
                // now try to open the device
                fileHandle = open(deviceNode, O_RDONLY | O_NONBLOCK, 0);
                if (fileHandle >= 0)
                {
                    cdrom_subchnl cdChannelInfo;
                    cdChannelInfo.cdsc_format = CDROM_MSF;
                    // this call will finally reveal the whole truth
#ifdef RT_OS_LINUX
                    if ((ioctl(fileHandle, CDROMSUBCHNL, &cdChannelInfo) == 0) ||
                        (errno == EIO) || (errno == ENOENT) ||
                        (errno == EINVAL) || (errno == ENOMEDIUM))
#else
                    if ((ioctl(fileHandle, CDROMSUBCHNL, &cdChannelInfo) == 0) ||
                        (errno == EIO) || (errno == ENOENT) ||
                        (errno == EINVAL))
#endif
                    {
                        retValue = true;
                    }
                    close(fileHandle);
                }
            }
        } else
        {
            // floppy case
            if (S_ISCHR(statInfo.st_mode) || S_ISBLK(statInfo.st_mode))
            {
                /// @todo do some more testing, maybe a nice IOCTL!
                retValue = true;
            }
        }
    }
    return retValue;
}
#endif // RT_OS_SOLARIS

#ifdef VBOX_WITH_USB
/**
 *  Checks for the presence and status of the USB Proxy Service.
 *  Returns S_OK when the Proxy is present and OK, VBOX_E_HOST_ERROR (as a
 *  warning) if the proxy service is not available due to the way the host is
 *  configured (at present, that means that usbfs and hal/DBus are not
 *  available on a Linux host) or E_FAIL and a corresponding error message
 *  otherwise. Intended to be used by methods that rely on the Proxy Service
 *  availability.
 *
 *  @note This method may return a warning result code. It is recommended to use
 *        MultiError to store the return value.
 *
 *  @note Locks this object for reading.
 */
HRESULT Host::i_checkUSBProxyService()
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertReturn(m->pUSBProxyService, E_FAIL);
    if (!m->pUSBProxyService->isActive())
    {
        /* disable the USB controller completely to avoid assertions if the
         * USB proxy service could not start. */

        switch (m->pUSBProxyService->getLastError())
        {
            case VERR_FILE_NOT_FOUND:  /** @todo what does this mean? */
                return setWarning(E_FAIL,
                                  tr("Could not load the Host USB Proxy Service (VERR_FILE_NOT_FOUND).  The service might not be installed on the host computer"));
            case VERR_VUSB_USB_DEVICE_PERMISSION:
                return setWarning(E_FAIL,
                                  tr("VirtualBox is not currently allowed to access USB devices.  You can change this by adding your user to the 'vboxusers' group.  Please see the user manual for a more detailed explanation"));
            case VERR_VUSB_USBFS_PERMISSION:
                return setWarning(E_FAIL,
                                  tr("VirtualBox is not currently allowed to access USB devices.  You can change this by allowing your user to access the 'usbfs' folder and files.  Please see the user manual for a more detailed explanation"));
            case VINF_SUCCESS:
                return setWarning(E_FAIL,
                                  tr("The USB Proxy Service has not yet been ported to this host"));
            default:
                return setWarning(E_FAIL, "%s: %Rrc",
                                  tr("Could not load the Host USB Proxy service"),
                                  m->pUSBProxyService->getLastError());
        }
    }

    return S_OK;
}
#endif /* VBOX_WITH_USB */

HRESULT Host::i_updateNetIfList()
{
#ifdef VBOX_WITH_HOSTNETIF_API
    AssertReturn(!isWriteLockOnCurrentThread(), E_FAIL);

    /** @todo r=klaus it would save lots of clock cycles if for concurrent
     * threads executing this code we'd only do one interface enumeration
     * and update, and let the other threads use the result as is. However
     * if there's a constant hammering of this method, we don't want this
     * to cause update starvation. */
    HostNetworkInterfaceList list;
    int vrc = NetIfList(list);
    if (RT_FAILURE(vrc))
    {
        Log(("Failed to get host network interface list with vrc=%Rrc\n", vrc));
        return E_FAIL;
    }

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertReturn(m->pParent, E_FAIL);
    /* Make a copy as the original may be partially destroyed later. */
    HostNetworkInterfaceList listCopy(list);
    HostNetworkInterfaceList::iterator itOld, itNew;
# ifdef VBOX_WITH_RESOURCE_USAGE_API
    PerformanceCollector *aCollector = m->pParent->i_performanceCollector();
# endif
    for (itOld = m->llNetIfs.begin(); itOld != m->llNetIfs.end(); ++itOld)
    {
        bool fGone = true;
        Bstr nameOld;
        (*itOld)->COMGETTER(Name)(nameOld.asOutParam());
        for (itNew = listCopy.begin(); itNew != listCopy.end(); ++itNew)
        {
            Bstr nameNew;
            (*itNew)->COMGETTER(Name)(nameNew.asOutParam());
            if (nameNew == nameOld)
            {
                fGone = false;
                (*itNew)->uninit();
                listCopy.erase(itNew);
                break;
            }
        }
        if (fGone)
        {
# ifdef VBOX_WITH_RESOURCE_USAGE_API
            (*itOld)->i_unregisterMetrics(aCollector, this);
            (*itOld)->uninit();
# endif
        }
    }
    /*
     * Need to set the references to VirtualBox object in all interface objects
     * (see @bugref{6439}).
     */
    for (itNew = list.begin(); itNew != list.end(); ++itNew)
        (*itNew)->i_setVirtualBox(m->pParent);
    /* At this point listCopy will contain newly discovered interfaces only. */
    for (itNew = listCopy.begin(); itNew != listCopy.end(); ++itNew)
    {
        HostNetworkInterfaceType_T t;
        HRESULT hrc = (*itNew)->COMGETTER(InterfaceType)(&t);
        if (FAILED(hrc))
        {
            Bstr n;
            (*itNew)->COMGETTER(Name)(n.asOutParam());
            LogRel(("Host::updateNetIfList: failed to get interface type for %ls\n", n.raw()));
        }
        else if (t == HostNetworkInterfaceType_Bridged)
        {
# ifdef VBOX_WITH_RESOURCE_USAGE_API
            (*itNew)->i_registerMetrics(aCollector, this);
# endif
        }
    }
    m->llNetIfs = list;
    return S_OK;
#else
    return E_NOTIMPL;
#endif
}

#ifdef VBOX_WITH_RESOURCE_USAGE_API

void Host::i_registerDiskMetrics(PerformanceCollector *aCollector)
{
    pm::CollectorHAL *hal = aCollector->getHAL();
    /* Create sub metrics */
    Utf8StrFmt fsNameBase("FS/{%s}/Usage", "/");
    //Utf8StrFmt fsNameBase("Filesystem/[root]/Usage");
    pm::SubMetric *fsRootUsageTotal  = new pm::SubMetric(fsNameBase + "/Total",
        "Root file system size.");
    pm::SubMetric *fsRootUsageUsed   = new pm::SubMetric(fsNameBase + "/Used",
        "Root file system space currently occupied.");
    pm::SubMetric *fsRootUsageFree   = new pm::SubMetric(fsNameBase + "/Free",
        "Root file system space currently empty.");

    pm::BaseMetric *fsRootUsage = new pm::HostFilesystemUsage(hal, this,
                                                              fsNameBase, "/",
                                                              fsRootUsageTotal,
                                                              fsRootUsageUsed,
                                                              fsRootUsageFree);
    aCollector->registerBaseMetric(fsRootUsage);

    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageTotal, 0));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageTotal,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageTotal,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageTotal,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageUsed, 0));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageUsed,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageUsed,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageUsed,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageFree, 0));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageFree,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageFree,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(fsRootUsage, fsRootUsageFree,
                                              new pm::AggregateMax()));

    /* For now we are concerned with the root file system only. */
    pm::DiskList disksUsage, disksLoad;
    int vrc = hal->getDiskListByFs("/", disksUsage, disksLoad);
    if (RT_FAILURE(vrc))
        return;
    pm::DiskList::iterator it;
    for (it = disksLoad.begin(); it != disksLoad.end(); ++it)
    {
        Utf8StrFmt strName("Disk/%s", it->c_str());
        pm::SubMetric *fsLoadUtil   = new pm::SubMetric(strName + "/Load/Util",
            "Percentage of time disk was busy serving I/O requests.");
        pm::BaseMetric *fsLoad  = new pm::HostDiskLoadRaw(hal, this, strName + "/Load",
                                                         *it, fsLoadUtil);
        aCollector->registerBaseMetric(fsLoad);

        aCollector->registerMetric(new pm::Metric(fsLoad, fsLoadUtil, 0));
        aCollector->registerMetric(new pm::Metric(fsLoad, fsLoadUtil,
                                                  new pm::AggregateAvg()));
        aCollector->registerMetric(new pm::Metric(fsLoad, fsLoadUtil,
                                                  new pm::AggregateMin()));
        aCollector->registerMetric(new pm::Metric(fsLoad, fsLoadUtil,
                                                  new pm::AggregateMax()));
    }
    for (it = disksUsage.begin(); it != disksUsage.end(); ++it)
    {
        Utf8StrFmt strName("Disk/%s", it->c_str());
        pm::SubMetric *fsUsageTotal = new pm::SubMetric(strName + "/Usage/Total",
            "Disk size.");
        pm::BaseMetric *fsUsage = new pm::HostDiskUsage(hal, this, strName + "/Usage",
                                                        *it, fsUsageTotal);
        aCollector->registerBaseMetric(fsUsage);

        aCollector->registerMetric(new pm::Metric(fsUsage, fsUsageTotal, 0));
        aCollector->registerMetric(new pm::Metric(fsUsage, fsUsageTotal,
                                                  new pm::AggregateAvg()));
        aCollector->registerMetric(new pm::Metric(fsUsage, fsUsageTotal,
                                                  new pm::AggregateMin()));
        aCollector->registerMetric(new pm::Metric(fsUsage, fsUsageTotal,
                                                  new pm::AggregateMax()));
    }
}

void Host::i_registerMetrics(PerformanceCollector *aCollector)
{
    pm::CollectorHAL *hal = aCollector->getHAL();
    /* Create sub metrics */
    pm::SubMetric *cpuLoadUser   = new pm::SubMetric("CPU/Load/User",
        "Percentage of processor time spent in user mode.");
    pm::SubMetric *cpuLoadKernel = new pm::SubMetric("CPU/Load/Kernel",
        "Percentage of processor time spent in kernel mode.");
    pm::SubMetric *cpuLoadIdle   = new pm::SubMetric("CPU/Load/Idle",
        "Percentage of processor time spent idling.");
    pm::SubMetric *cpuMhzSM      = new pm::SubMetric("CPU/MHz",
        "Average of current frequency of all processors.");
    pm::SubMetric *ramUsageTotal = new pm::SubMetric("RAM/Usage/Total",
        "Total physical memory installed.");
    pm::SubMetric *ramUsageUsed  = new pm::SubMetric("RAM/Usage/Used",
        "Physical memory currently occupied.");
    pm::SubMetric *ramUsageFree  = new pm::SubMetric("RAM/Usage/Free",
        "Physical memory currently available to applications.");
    pm::SubMetric *ramVMMUsed = new pm::SubMetric("RAM/VMM/Used",
        "Total physical memory used by the hypervisor.");
    pm::SubMetric *ramVMMFree = new pm::SubMetric("RAM/VMM/Free",
        "Total physical memory free inside the hypervisor.");
    pm::SubMetric *ramVMMBallooned  = new pm::SubMetric("RAM/VMM/Ballooned",
        "Total physical memory ballooned by the hypervisor.");
    pm::SubMetric *ramVMMShared = new pm::SubMetric("RAM/VMM/Shared",
        "Total physical memory shared between VMs.");


    /* Create and register base metrics */
    pm::BaseMetric *cpuLoad = new pm::HostCpuLoadRaw(hal, this, cpuLoadUser, cpuLoadKernel,
                                          cpuLoadIdle);
    aCollector->registerBaseMetric(cpuLoad);
    pm::BaseMetric *cpuMhz = new pm::HostCpuMhz(hal, this, cpuMhzSM);
    aCollector->registerBaseMetric(cpuMhz);
    pm::BaseMetric *ramUsage = new pm::HostRamUsage(hal, this,
                                                    ramUsageTotal,
                                                    ramUsageUsed,
                                                    ramUsageFree);
    aCollector->registerBaseMetric(ramUsage);
    pm::BaseMetric *ramVmm = new pm::HostRamVmm(aCollector->getGuestManager(), this,
                                                ramVMMUsed,
                                                ramVMMFree,
                                                ramVMMBallooned,
                                                ramVMMShared);
    aCollector->registerBaseMetric(ramVmm);

    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadUser, 0));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadUser,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadUser,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadUser,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadKernel, 0));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadKernel,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadKernel,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadKernel,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadIdle, 0));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadIdle,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadIdle,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(cpuLoad, cpuLoadIdle,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(cpuMhz, cpuMhzSM, 0));
    aCollector->registerMetric(new pm::Metric(cpuMhz, cpuMhzSM,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(cpuMhz, cpuMhzSM,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(cpuMhz, cpuMhzSM,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageTotal, 0));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageTotal,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageTotal,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageTotal,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageUsed, 0));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageUsed,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageUsed,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageUsed,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageFree, 0));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageFree,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageFree,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(ramUsage, ramUsageFree,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMUsed, 0));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMUsed,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMUsed,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMUsed,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMFree, 0));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMFree,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMFree,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMFree,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMBallooned, 0));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMBallooned,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMBallooned,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMBallooned,
                                              new pm::AggregateMax()));

    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMShared, 0));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMShared,
                                              new pm::AggregateAvg()));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMShared,
                                              new pm::AggregateMin()));
    aCollector->registerMetric(new pm::Metric(ramVmm, ramVMMShared,
                                              new pm::AggregateMax()));
    i_registerDiskMetrics(aCollector);
}

void Host::i_unregisterMetrics(PerformanceCollector *aCollector)
{
    aCollector->unregisterMetricsFor(this);
    aCollector->unregisterBaseMetricsFor(this);
}

#endif /* VBOX_WITH_RESOURCE_USAGE_API */


/* static */
void Host::i_generateMACAddress(Utf8Str &mac)
{
    /*
     * Our strategy is as follows: the first three bytes are our fixed
     * vendor ID (080027). The remaining 3 bytes will be taken from the
     * start of a GUID. This is a fairly safe algorithm.
     */
    Guid guid;
    guid.create();
    mac = Utf8StrFmt("080027%02X%02X%02X",
                     guid.raw()->au8[0], guid.raw()->au8[1], guid.raw()->au8[2]);
}

/**
 * Returns the host's platform architecture.
 *
 * @returns The host's platform architecture.
 */
/* static */
PlatformArchitecture_T Host::s_getPlatformArchitecture()
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

#ifdef RT_OS_WINDOWS
HRESULT Host::i_getFixedDrivesFromGlobalNamespace(std::list<std::pair<com::Utf8Str, com::Utf8Str> > &aDriveList) RT_NOEXCEPT
{
    RTERRINFOSTATIC  ErrInfo;
    uint32_t         offError;
    RTVFSDIR         hVfsDir;
    int vrc = RTVfsChainOpenDir("\\\\:iprtnt:\\GLOBAL??", 0 /*fFlags*/, &hVfsDir, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(vrc))
        return setError(E_FAIL, tr("Failed to open NT\\GLOBAL?? (error %Rrc)"), vrc);

    /*
     * Scan whole directory and find any 'PhysicalDiskX' entries. Next, combine with '\\.\'
     * to obtain the harddisk dev path.
     */
    size_t          cbDirEntryAlloced = sizeof(RTDIRENTRYEX);
    PRTDIRENTRYEX   pDirEntry         = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
    if (!pDirEntry)
    {
        RTVfsDirRelease(hVfsDir);
        return setError(E_OUTOFMEMORY, tr("Out of memory! (direntry buffer)"));
    }

    HRESULT hrc = S_OK;
    for (;;)
    {
        size_t cbDirEntry = cbDirEntryAlloced;
        vrc = RTVfsDirReadEx(hVfsDir, pDirEntry, &cbDirEntry, RTFSOBJATTRADD_NOTHING);
        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_BUFFER_OVERFLOW)
            {
                RTMemTmpFree(pDirEntry);
                cbDirEntryAlloced = RT_ALIGN_Z(RT_MIN(cbDirEntry, cbDirEntryAlloced) + 64, 64);
                pDirEntry  = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
                if (pDirEntry)
                    continue;
                hrc = setError(E_OUTOFMEMORY, tr("Out of memory! (direntry buffer)"));
            }
            else if (vrc != VERR_NO_MORE_FILES)
                hrc = setError(VBOX_E_IPRT_ERROR, tr("RTVfsDirReadEx failed: %Rrc"), vrc);
            break;
        }
        if (RTStrStartsWith(pDirEntry->szName, "PhysicalDrive"))
        {
            char szPhysicalDrive[64];
            RTStrPrintf(szPhysicalDrive, sizeof(szPhysicalDrive), "\\\\.\\%s", pDirEntry->szName);

            RTFILE hRawFile = NIL_RTFILE;
            vrc = RTFileOpen(&hRawFile, szPhysicalDrive, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
            if (RT_FAILURE(vrc))
            {
                try
                {
                    aDriveList.push_back(std::pair<com::Utf8Str, com::Utf8Str>(szPhysicalDrive, tr("Unknown (Access denied)")));
                }
                catch (std::bad_alloc &)
                {
                    hrc = setError(E_OUTOFMEMORY, tr("Out of memory"));
                    break;
                }
                continue;
            }

            DWORD   cbBytesReturned = 0;
            uint8_t abBuffer[1024];
            RT_ZERO(abBuffer);

            STORAGE_PROPERTY_QUERY query;
            RT_ZERO(query);
            query.PropertyId = StorageDeviceProperty;
            query.QueryType  = PropertyStandardQuery;

            BOOL fRc = DeviceIoControl((HANDLE)RTFileToNative(hRawFile),
                                       IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                                       abBuffer, sizeof(abBuffer), &cbBytesReturned, NULL);
            RTFileClose(hRawFile);
            char szModel[1024];
            if (fRc)
            {
                PSTORAGE_DEVICE_DESCRIPTOR pDevDescriptor = (PSTORAGE_DEVICE_DESCRIPTOR)abBuffer;
                char *pszProduct = pDevDescriptor->ProductIdOffset ? (char *)&abBuffer[pDevDescriptor->ProductIdOffset] : NULL;
                if (pszProduct)
                {
                    RTStrPurgeEncoding(pszProduct);
                    if (*pszProduct != '\0')
                    {
                        char *pszVendor = pDevDescriptor->VendorIdOffset  ? (char *)&abBuffer[pDevDescriptor->VendorIdOffset] : NULL;
                        if (pszVendor)
                            RTStrPurgeEncoding(pszVendor);
                        if (pszVendor && *pszVendor)
                            RTStrPrintf(szModel, sizeof(szModel), "%s %s", pszVendor, pszProduct);
                        else
                            RTStrCopy(szModel, sizeof(szModel), pszProduct);
                    }
                }
            }
            try
            {
                aDriveList.push_back(std::pair<com::Utf8Str, com::Utf8Str>(szPhysicalDrive, szModel));
            }
            catch (std::bad_alloc &)
            {
                hrc = setError(E_OUTOFMEMORY, tr("Out of memory"));
                break;
            }
        }
    }
    if (FAILED(hrc))
        aDriveList.clear();
    RTMemTmpFree(pDirEntry);
    RTVfsDirRelease(hVfsDir);
    return hrc;
}
#endif

/**
 * @throws nothing
 */
HRESULT Host::i_getDrivesPathsList(std::list<std::pair<com::Utf8Str, com::Utf8Str> > &aDriveList) RT_NOEXCEPT
{
#ifdef RT_OS_WINDOWS
    return i_getFixedDrivesFromGlobalNamespace(aDriveList);

#elif defined(RT_OS_DARWIN)
    /*
     * Get the list of fixed drives from iokit.cpp and transfer it to aDriveList.
     */
    PDARWINFIXEDDRIVE pDrives = DarwinGetFixedDrives();
    HRESULT hrc;
    try
    {
        for (PDARWINFIXEDDRIVE pCurDrv = pDrives; pCurDrv; pCurDrv = pCurDrv->pNext)
            aDriveList.push_back(std::pair<com::Utf8Str, com::Utf8Str>(pCurDrv->szName, pCurDrv->pszModel));
        hrc = S_OK;
    }
    catch (std::bad_alloc &)
    {
        aDriveList.clear();
        hrc = E_OUTOFMEMORY;
    }

    while (pDrives)
    {
        PDARWINFIXEDDRIVE pFreeMe = pDrives;
        pDrives = pDrives->pNext;
        ASMCompilerBarrier();
        RTMemFree(pFreeMe);
    }
    return hrc;

#elif defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
    /*
     * The list of fixed drives is kept in the VBoxMainDriveInfo instance, so
     * update it and tranfer the info to aDriveList.
     *
     * This obviously requires us to write lock the object!
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    int vrc = m->hostDrives.updateFixedDrives(); /* nothrow */
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Failed to update fixed drive list (%Rrc)"), vrc);

    try
    {
        for (DriveInfoList::const_iterator it = m->hostDrives.FixedDriveBegin(); it != m->hostDrives.FixedDriveEnd(); ++it)
            aDriveList.push_back(std::pair<com::Utf8Str, com::Utf8Str>(it->mDevice, it->mDescription));
    }
    catch (std::bad_alloc &)
    {
        aDriveList.clear();
        return E_OUTOFMEMORY;
    }
    return S_OK;

#elif defined(RT_OS_SOLARIS)
    /*
     * We can get the info from HAL, if not present/working we'll get by
     * walking the device tree.
     */
# ifdef VBOX_USE_LIBHAL
    HRESULT hrc = i_getFixedDrivesFromHal(aDriveList);
    if (hrc != S_FALSE)
        return hrc;
    aDriveList.clear(); /* just in case */
# endif
    return i_getFixedDrivesFromDevTree(aDriveList);

#else
    /* PORTME */
    RT_NOREF(aDriveList);
    return E_NOTIMPL;
#endif
}


#ifdef VBOX_WITH_NATIVE_NEM
BOOL Host::i_HostIsNativeApiSupported()
{
# ifdef RT_OS_WINDOWS
    WCHAR wszPath[MAX_PATH + 64];
    UINT  cwcPath = GetSystemDirectoryW(wszPath, MAX_PATH);
    if (cwcPath >= MAX_PATH || cwcPath < 2)
        return FALSE;

    if (wszPath[cwcPath - 1] != '\\' || wszPath[cwcPath - 1] != '/')
        wszPath[cwcPath++] = '\\';
    RTUtf16CopyAscii(&wszPath[cwcPath], RT_ELEMENTS(wszPath) - cwcPath, "WinHvPlatform.dll");
    if (GetFileAttributesW(wszPath) == INVALID_FILE_ATTRIBUTES)
        return FALSE;

#  ifdef RT_ARCH_AMD64
    /*
     * Check that we're in a VM and that the hypervisor identifies itself as Hyper-V.
     */
    if (!ASMHasCpuId())
        return FALSE;
    if (!RTX86IsValidStdRange(ASMCpuId_EAX(0)))
        return FALSE;
    if (!(ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_HVP))
        return FALSE;

    uint32_t cMaxHyperLeaf = 0;
    uint32_t uEbx = 0;
    uint32_t uEcx = 0;
    uint32_t uEdx = 0;
    ASMCpuIdExSlow(0x40000000, 0, 0, 0, &cMaxHyperLeaf, &uEbx, &uEcx, &uEdx);
    if (!RTX86IsValidHypervisorRange(cMaxHyperLeaf))
        return FALSE;
    if (   uEbx != UINT32_C(0x7263694d) /* Micr */
        || uEcx != UINT32_C(0x666f736f) /* osof */
        || uEdx != UINT32_C(0x76482074) /* t Hv */)
        return FALSE;
    if (cMaxHyperLeaf >= UINT32_C(0x40000005))
        return TRUE;
#  endif
# elif defined(RT_OS_LINUX)
    int fdKvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (fdKvm >= 0)
    {
        /** @todo Do we need to do anything else here? */
        close(fdKvm);
        return TRUE;
    }
# elif defined(RT_OS_DARWIN)
    /*
     * The kern.hv_support parameter indicates support for the hypervisor API
     * in the kernel.
     */
    int32_t fHvSupport = 0;
    size_t  cbOld = sizeof(fHvSupport);
    if (sysctlbyname("kern.hv_support", &fHvSupport, &cbOld, NULL, 0) == 0)
    {
        if (fHvSupport != 0)
            return TRUE;
    }
# endif
    return FALSE;
}
#endif


/* vi: set tabstop=4 shiftwidth=4 expandtab: */
