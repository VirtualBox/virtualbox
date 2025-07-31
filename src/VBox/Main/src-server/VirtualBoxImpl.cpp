/* $Id: VirtualBoxImpl.cpp 110340 2025-07-21 14:14:57Z klaus.espenlaub@oracle.com $ */
/** @file
 * Implementation of IVirtualBox in VBoxSVC.
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

#define LOG_GROUP LOG_GROUP_MAIN_VIRTUALBOX
#include <iprt/asm.h>
#include <iprt/base64.h>
#include <iprt/buildconfig.h>
#include <iprt/cpp/utils.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/system.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#include <iprt/cpp/xml.h>
#include <iprt/ctype.h>

#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include "VBox/com/EventQueue.h"
#include "VBox/com/MultiResult.h"

#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/settings.h>
#include <VBox/sup.h>
#include <VBox/version.h>

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

#include <package-generated.h>

#include <algorithm>
#include <set>
#include <vector>
#include <memory> // for auto_ptr

#include "VirtualBoxImpl.h"

#include "Global.h"
#include "MachineImpl.h"
#include "MediumImpl.h"
#include "SharedFolderImpl.h"
#include "ProgressImpl.h"
#include "HostImpl.h"
#include "USBControllerImpl.h"
#include "SystemPropertiesImpl.h"
#include "GuestOSTypeImpl.h"
#include "NetworkServiceRunner.h"
#include "DHCPServerImpl.h"
#include "NATNetworkImpl.h"
#ifdef VBOX_WITH_VMNET
#include "HostOnlyNetworkImpl.h"
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
#include "CloudNetworkImpl.h"
#endif /* VBOX_WITH_CLOUD_NET */
#ifdef VBOX_WITH_RESOURCE_USAGE_API
# include "PerformanceImpl.h"
#endif /* VBOX_WITH_RESOURCE_USAGE_API */
#ifdef VBOX_WITH_UPDATE_AGENT
# include "UpdateAgentImpl.h"
#endif
#include "EventImpl.h"
#ifdef VBOX_WITH_EXTPACK
# include "ExtPackManagerImpl.h"
#endif
#ifdef VBOX_WITH_UNATTENDED
# include "UnattendedImpl.h"
#endif
#include "AutostartDb.h"
#include "ClientWatcher.h"
#include "AutoCaller.h"
#include "LoggingNew.h"
#include "CloudProviderManagerImpl.h"
#include "ThreadTask.h"
#include "VBoxEvents.h"
#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
# include "ObjectsTracker.h"
#endif

#include <QMTranslator.h>

#ifdef RT_OS_WINDOWS
# include "win/svchlp.h"
# include "tchar.h"
#endif


////////////////////////////////////////////////////////////////////////////////
//
// Definitions
//
////////////////////////////////////////////////////////////////////////////////

#define VBOX_GLOBAL_SETTINGS_FILE "VirtualBox.xml"

////////////////////////////////////////////////////////////////////////////////
//
// Global variables
//
////////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
extern TrackedObjectsCollector gTrackedObjectsCollector;
#endif

// static
com::Utf8Str VirtualBox::sVersion;

// static
com::Utf8Str VirtualBox::sVersionNormalized;

// static
ULONG VirtualBox::sRevision;

// static
com::Utf8Str VirtualBox::sPackageType;

// static
com::Utf8Str VirtualBox::sAPIVersion;

// static
std::map<com::Utf8Str, int> VirtualBox::sNatNetworkNameToRefCount;

// static leaked (todo: find better place to free it.)
RWLockHandle *VirtualBox::spMtxNatNetworkNameToRefCountLock;


////////////////////////////////////////////////////////////////////////////////
//
// AsyncEvent class
//
////////////////////////////////////////////////////////////////////////////////

/**
 * For firing off an event on asynchronously on an event thread.
 */
class VirtualBox::AsyncEvent : public Event
{
public:
    AsyncEvent(VirtualBox *a_pVirtualBox, ComPtr<IEvent> const &a_rEvent)
        : mVirtualBox(a_pVirtualBox), mEvent(a_rEvent)
    {
        Assert(a_pVirtualBox);
    }

    void *handler() RT_OVERRIDE;

private:
    /**
     * @note This is a weak ref -- the CallbackEvent handler thread is bound to the
     *       lifetime of the VirtualBox instance, so it's safe.
     */
    VirtualBox         *mVirtualBox;
    /** The event. */
    ComPtr<IEvent>      mEvent;
};

////////////////////////////////////////////////////////////////////////////////
//
// VirtualBox private member data definition
//
////////////////////////////////////////////////////////////////////////////////

#if defined(RT_OS_WINDOWS) && defined(VBOXSVC_WITH_CLIENT_WATCHER)
/**
 * Client process watcher data.
 */
class WatchedClientProcess
{
public:
    WatchedClientProcess(RTPROCESS a_pid, HANDLE a_hProcess) RT_NOEXCEPT
        : m_pid(a_pid)
        , m_cRefs(1)
        , m_hProcess(a_hProcess)
    {
    }

    ~WatchedClientProcess()
    {
        if (m_hProcess != NULL)
        {
            ::CloseHandle(m_hProcess);
            m_hProcess = NULL;
        }
        m_pid = NIL_RTPROCESS;
    }

    /** The client PID.   */
    RTPROCESS           m_pid;
    /** Number of references to this structure. */
    uint32_t volatile   m_cRefs;
    /** Handle of the client process.
     * Ideally, we've got full query privileges, but we'll settle for waiting.  */
    HANDLE              m_hProcess;
};
typedef std::map<RTPROCESS, WatchedClientProcess *> WatchedClientProcessMap;
#endif

typedef ObjectsList<Medium> MediaOList;
typedef ObjectsList<GuestOSType> GuestOSTypesOList;
typedef ObjectsList<SharedFolder> SharedFoldersOList;
typedef ObjectsList<DHCPServer> DHCPServersOList;
typedef ObjectsList<NATNetwork> NATNetworksOList;
#ifdef VBOX_WITH_VMNET
typedef ObjectsList<HostOnlyNetwork> HostOnlyNetworksOList;
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
typedef ObjectsList<CloudNetwork> CloudNetworksOList;
#endif /* VBOX_WITH_CLOUD_NET */

typedef std::map<Guid, ComPtr<IProgress> > ProgressMap;
typedef std::map<Guid, ComObjPtr<Medium> > HardDiskMap;

/**
 *  Main VirtualBox data structure.
 *  @note |const| members are persistent during lifetime so can be accessed
 *  without locking.
 */
struct VirtualBox::Data
{
    Data()
        : pMainConfigFile(NULL)
        , uuidMediaRegistry("48024e5c-fdd9-470f-93af-ec29f7ea518c")
        , uRegistryNeedsSaving(0)
        , lockMachines(LOCKCLASS_LISTOFMACHINES, "Machines")
        , allMachines(lockMachines)
        , lockGuestOSTypes(LOCKCLASS_LISTOFOTHEROBJECTS, "GuestOSTypes")
        , allGuestOSTypes(lockGuestOSTypes)
        , lockMedia(LOCKCLASS_LISTOFMEDIA, "Media")
        , allHardDisks(lockMedia)
        , allDVDImages(lockMedia)
        , allFloppyImages(lockMedia)
        , lockSharedFolders(LOCKCLASS_LISTOFOTHEROBJECTS, "SharedFolders")
        , allSharedFolders(lockSharedFolders)
        , lockDHCPServers(LOCKCLASS_LISTOFOTHEROBJECTS, "DHCPServers")
        , allDHCPServers(lockDHCPServers)
        , lockNATNetworks(LOCKCLASS_LISTOFOTHEROBJECTS, "NATNetworks")
        , allNATNetworks(lockNATNetworks)
#ifdef VBOX_WITH_VMNET
        , lockHostOnlyNetworks(LOCKCLASS_LISTOFOTHEROBJECTS, "HostOnlyNetworks")
        , allHostOnlyNetworks(lockHostOnlyNetworks)
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
        , lockCloudNetworks(LOCKCLASS_LISTOFOTHEROBJECTS, "CloudNetworks")
        , allCloudNetworks(lockCloudNetworks)
#endif /* VBOX_WITH_CLOUD_NET */
        , mtxProgressOperations(LOCKCLASS_PROGRESSLIST, "ProgressOperations")
        , pClientWatcher(NULL)
        , threadAsyncEvent(NIL_RTTHREAD)
        , pAsyncEventQ(NULL)
        , pAutostartDb(NULL)
        , fSettingsCipherKeySet(false)
#ifdef VBOX_WITH_MAIN_NLS
        , pVBoxTranslator(NULL)
        , pTrComponent(NULL)
#endif
#if defined(RT_OS_WINDOWS) && defined(VBOXSVC_WITH_CLIENT_WATCHER)
        , fWatcherIsReliable(RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
#endif
        , hLdrModCrypto(NIL_RTLDRMOD)
        , cRefsCrypto(0)
        , pCryptoIf(NULL)
#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
        , objectTrackerTask(NULL)
#endif
    {
#if defined(RT_OS_WINDOWS) && defined(VBOXSVC_WITH_CLIENT_WATCHER)
        RTCritSectRwInit(&WatcherCritSect);
#endif
    }

    ~Data()
    {
        if (pMainConfigFile)
        {
            delete pMainConfigFile;
            pMainConfigFile = NULL;
        }
    };

    // const data members not requiring locking
    const Utf8Str                       strHomeDir;

    // VirtualBox main settings file
    const Utf8Str                       strSettingsFilePath;
    settings::MainConfigFile            *pMainConfigFile;

    // constant pseudo-machine ID for global media registry
    const Guid                          uuidMediaRegistry;

    // counter if global media registry needs saving, updated using atomic
    // operations, without requiring any locks
    uint64_t                            uRegistryNeedsSaving;

    // const objects not requiring locking
    const ComObjPtr<Host>               pHost;
    const ComObjPtr<SystemProperties>   pSystemProperties;
#ifdef VBOX_WITH_RESOURCE_USAGE_API
    const ComObjPtr<PerformanceCollector> pPerformanceCollector;
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

    // Each of the following lists use a particular lock handle that protects the
    // list as a whole. As opposed to version 3.1 and earlier, these lists no
    // longer need the main VirtualBox object lock, but only the respective list
    // lock. In each case, the locking order is defined that the list must be
    // requested before object locks of members of the lists (see the order definitions
    // in AutoLock.h; e.g. LOCKCLASS_LISTOFMACHINES before LOCKCLASS_MACHINEOBJECT).
    RWLockHandle                        lockMachines;
    MachinesOList                       allMachines;

    RWLockHandle                        lockGuestOSTypes;
    GuestOSTypesOList                   allGuestOSTypes;

    // All the media lists are protected by the following locking handle:
    RWLockHandle                        lockMedia;
    MediaOList                          allHardDisks,           // base images only!
                                        allDVDImages,
                                        allFloppyImages;
    // the hard disks map is an additional map sorted by UUID for quick lookup
    // and contains ALL hard disks (base and differencing); it is protected by
    // the same lock as the other media lists above
    HardDiskMap                         mapHardDisks;

    // list of pending machine renames (also protected by media tree lock;
    // see VirtualBox::rememberMachineNameChangeForMedia())
    struct PendingMachineRename
    {
        Utf8Str     strConfigDirOld;
        Utf8Str     strConfigDirNew;
    };
    typedef std::list<PendingMachineRename> PendingMachineRenamesList;
    PendingMachineRenamesList           llPendingMachineRenames;

    RWLockHandle                        lockSharedFolders;
    SharedFoldersOList                  allSharedFolders;

    RWLockHandle                        lockDHCPServers;
    DHCPServersOList                    allDHCPServers;

    RWLockHandle                        lockNATNetworks;
    NATNetworksOList                    allNATNetworks;

#ifdef VBOX_WITH_VMNET
    RWLockHandle                        lockHostOnlyNetworks;
    HostOnlyNetworksOList               allHostOnlyNetworks;
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
    RWLockHandle                        lockCloudNetworks;
    CloudNetworksOList                  allCloudNetworks;
#endif /* VBOX_WITH_CLOUD_NET */

    RWLockHandle                        mtxProgressOperations;
    ProgressMap                         mapProgressOperations;

    ClientWatcher * const               pClientWatcher;

    // the following are data for the async event thread
    const RTTHREAD                      threadAsyncEvent;
    EventQueue * const                  pAsyncEventQ;
    const ComObjPtr<EventSource>        pEventSource;

#ifdef VBOX_WITH_EXTPACK
    /** The extension pack manager object lives here. */
    const ComObjPtr<ExtPackManager>     ptrExtPackManager;
#endif

    /** The reference to the cloud provider manager singleton. */
    const ComObjPtr<CloudProviderManager> pCloudProviderManager;

    /** The global autostart database for the user. */
    AutostartDb * const                 pAutostartDb;

    /** Settings secret */
    bool                                fSettingsCipherKeySet;
    uint8_t                             SettingsCipherKey[RTSHA512_HASH_SIZE];
#ifdef VBOX_WITH_MAIN_NLS
    VirtualBoxTranslator               *pVBoxTranslator;
    PTRCOMPONENT                        pTrComponent;
#endif
#if defined(RT_OS_WINDOWS) && defined(VBOXSVC_WITH_CLIENT_WATCHER)
    /** Critical section protecting WatchedProcesses. */
    RTCRITSECTRW                        WatcherCritSect;
    /** Map of processes being watched, key is the PID. */
    WatchedClientProcessMap             WatchedProcesses;
    /** Set if the watcher is reliable, otherwise cleared.
     * The watcher goes unreliable when we run out of memory, fail open a client
     * process, or if the watcher thread gets messed up. */
    bool                                fWatcherIsReliable;
#endif

    /** @name Members related to the cryptographic support interface.
     * @{ */
    /** The loaded module handle if loaded. */
    RTLDRMOD                            hLdrModCrypto;
    /** Reference counter tracking how many users of the cryptographic support
     * are there currently. */
    volatile uint32_t                   cRefsCrypto;
    /** Pointer to the cryptographic support interface. */
    PCVBOXCRYPTOIF                      pCryptoIf;
    /** Critical section protecting the module handle. */
    RTCRITSECT                          CritSectModCrypto;
    /** @} */

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
    /** The tracked object collector (better if it'll be a singleton) */
    ObjectTracker                      *objectTrackerTask;
#endif
};


/**
 * VirtualBox firmware descriptor.
 */
typedef struct VBOXFWDESC
{
    FirmwareType_T  enmType;
    bool            fBuiltIn;
    const char     *pszFileName;
    const char     *pszUrl;
} VBoxFwDesc;
typedef const VBOXFWDESC *PVBOXFWDESC;


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(VirtualBox)

HRESULT VirtualBox::FinalConstruct()
{
    LogRelFlowThisFuncEnter();
    LogRel(("VirtualBox: object creation starts\n"));

    BaseFinalConstruct();

    HRESULT hrc = init();

    LogRelFlowThisFuncLeave();
    LogRel(("VirtualBox: object created\n"));

    return hrc;
}

void VirtualBox::FinalRelease()
{
    LogRelFlowThisFuncEnter();
    LogRel(("VirtualBox: object deletion starts\n"));

    uninit();

    BaseFinalRelease();

    LogRel(("VirtualBox: object deleted\n"));
    LogRelFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the VirtualBox object.
 *
 *  @return COM result code
 */
HRESULT VirtualBox::init()
{
    LogRelFlowThisFuncEnter();
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Locking this object for writing during init sounds a bit paradoxical,
     * but in the current locking mess this avoids that some code gets a
     * read lock and later calls code which wants the same write lock. */
    AutoWriteLock lock(this COMMA_LOCKVAL_SRC_POS);

    // allocate our instance data
    m = new Data;

    LogFlow(("===========================================================\n"));
    LogFlowThisFuncEnter();

    if (sVersion.isEmpty())
        sVersion = RTBldCfgVersion();
    if (sVersionNormalized.isEmpty())
    {
        Utf8Str tmp(RTBldCfgVersion());
        if (tmp.endsWith(VBOX_BUILD_PUBLISHER))
            tmp = tmp.substr(0, tmp.length() - strlen(VBOX_BUILD_PUBLISHER));
        sVersionNormalized = tmp;
    }
    sRevision = RTBldCfgRevision();
    if (sPackageType.isEmpty())
        sPackageType = VBOX_PACKAGE_STRING;
    if (sAPIVersion.isEmpty())
        sAPIVersion = VBOX_API_VERSION_STRING;
    if (!spMtxNatNetworkNameToRefCountLock)
        spMtxNatNetworkNameToRefCountLock = new RWLockHandle(LOCKCLASS_VIRTUALBOXOBJECT, "spMtxNatNetworkNameToRefCountLock");

    LogFlowThisFunc(("Version: %s, Package: %s, API Version: %s\n", sVersion.c_str(), sPackageType.c_str(), sAPIVersion.c_str()));

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
    /* Try to start Object tracker thread as earlier as possible (same code in VirtualBoxClientImpl.cpp). */
    {
        int vrc = VERR_GENERAL_FAILURE;
        if (gTrackedObjectsCollector.init())
        {
            LogRel(("Starting the Object tracker thread\n"));
            try
            {
                m->objectTrackerTask = new ObjectTracker();
                if (m->objectTrackerTask->init()) // some init procedure - bird: some comment!
                    vrc = m->objectTrackerTask->createThread();
            }
            catch (...)
            {
                LogRel(("Exception during starting the Object tracker thread\n"));
                if (m->objectTrackerTask)
                {
                    delete m->objectTrackerTask;
                    m->objectTrackerTask = NULL;
                }
                vrc = VERR_INVALID_STATE;
            }
        }
        if (RT_SUCCESS(vrc))
            LogRel(("Successfully started the Object tracker thread\n"));
        else
            LogRel(("Failed to start the Object tracker thread (%Rrc)\n", vrc));
    }
#endif

    /* Important: DO NOT USE any kind of "early return" (except the single
     * one above, checking the init span success) in this method. It is vital
     * for correct error handling that it has only one point of return, which
     * does all the magic on COM to signal object creation success and
     * reporting the error later for every API method. COM translates any
     * unsuccessful object creation to REGDB_E_CLASSNOTREG errors or similar
     * unhelpful ones which cause us a lot of grief with troubleshooting. */

    HRESULT hrc = S_OK;
    bool fCreate = false;
    try
    {
        /* Create the event source early as we may fire async event during settings loading (media). */
        hrc = unconst(m->pEventSource).createObject();
        if (FAILED(hrc)) throw hrc;
        hrc = m->pEventSource->init();
        if (FAILED(hrc)) throw hrc;


        /* Get the VirtualBox home directory. */
        {
            char szHomeDir[RTPATH_MAX];
            int vrc = com::GetVBoxUserHomeDirectory(szHomeDir, sizeof(szHomeDir));
            if (RT_FAILURE(vrc))
                throw setErrorBoth(E_FAIL, vrc,
                                   tr("Could not create the VirtualBox home directory '%s' (%Rrc)"),
                                   szHomeDir, vrc);

            unconst(m->strHomeDir) = szHomeDir;
        }

        LogRel(("Home directory: '%s'\n", m->strHomeDir.c_str()));

        i_reportDriverVersions();

        /* Create the critical section protecting the cryptographic module handle. */
        {
            int vrc = RTCritSectInit(&m->CritSectModCrypto);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(E_FAIL, vrc,
                                   tr("Could not create the cryptographic module critical section (%Rrc)"),
                                   vrc);

        }

        /* compose the VirtualBox.xml file name */
        unconst(m->strSettingsFilePath) = Utf8StrFmt("%s%c%s",
                                                     m->strHomeDir.c_str(),
                                                     RTPATH_DELIMITER,
                                                     VBOX_GLOBAL_SETTINGS_FILE);
        // load and parse VirtualBox.xml; this will throw on XML or logic errors
        try
        {
            m->pMainConfigFile = new settings::MainConfigFile(&m->strSettingsFilePath);
        }
        catch (xml::EIPRTFailure &e)
        {
            // this is thrown by the XML backend if the RTOpen() call fails;
            // only if the main settings file does not exist, create it,
            // if there's something more serious, then do fail!
            if (e.getStatus() == VERR_FILE_NOT_FOUND)
                fCreate = true;
            else
                throw;
        }

        if (fCreate)
            m->pMainConfigFile = new settings::MainConfigFile(NULL);

#ifdef VBOX_WITH_RESOURCE_USAGE_API
        /* create the performance collector object BEFORE host */
        unconst(m->pPerformanceCollector).createObject();
        hrc = m->pPerformanceCollector->init();
        ComAssertComRCThrowRC(hrc);
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

        /* create the host object early, machines will need it */
        unconst(m->pHost).createObject();
        hrc = m->pHost->init(this);
        ComAssertComRCThrowRC(hrc);

        hrc = m->pHost->i_loadSettings(m->pMainConfigFile->host);
        if (FAILED(hrc)) throw hrc;

        /*
         * Create autostart database object early, because the system properties
         * might need it.
         */
        unconst(m->pAutostartDb) = new AutostartDb;

        /* create the system properties object, someone may need it too */
        hrc = unconst(m->pSystemProperties).createObject();
        if (SUCCEEDED(hrc))
            hrc = m->pSystemProperties->init(this);
        ComAssertComRCThrowRC(hrc);

        hrc = m->pSystemProperties->i_loadSettings(m->pMainConfigFile->systemProperties);
        if (FAILED(hrc)) throw hrc;
#ifdef VBOX_WITH_MAIN_NLS
        m->pVBoxTranslator = VirtualBoxTranslator::instance();
        /* Do not throw an exception on language errors.
         * Just do not use translation. */
        if (m->pVBoxTranslator)
        {

            char szNlsPath[RTPATH_MAX];
            int vrc = RTPathAppPrivateNoArch(szNlsPath, sizeof(szNlsPath));
            if (RT_SUCCESS(vrc))
                vrc = RTPathAppend(szNlsPath, sizeof(szNlsPath), "nls" RTPATH_SLASH_STR "VirtualBoxAPI");

            if (RT_SUCCESS(vrc))
            {
                vrc = m->pVBoxTranslator->registerTranslation(szNlsPath, true, &m->pTrComponent);
                if (RT_SUCCESS(vrc))
                {
                    com::Utf8Str strLocale;
                    HRESULT hrc2 = m->pSystemProperties->getLanguageId(strLocale);
                    if (SUCCEEDED(hrc2))
                    {
                        vrc = m->pVBoxTranslator->i_loadLanguage(strLocale.c_str());
                        if (RT_FAILURE(vrc))
                        {
                            hrc2 = Global::vboxStatusCodeToCOM(vrc);
                            LogRel(("Load language failed (%Rhrc).\n", hrc2));
                        }
                    }
                    else
                    {
                        LogRel(("Getting language settings failed (%Rhrc).\n", hrc2));
                        m->pVBoxTranslator->release();
                        m->pVBoxTranslator = NULL;
                        m->pTrComponent = NULL;
                    }
                }
                else
                {
                    HRESULT hrc2 = Global::vboxStatusCodeToCOM(vrc);
                    LogRel(("Register translation failed (%Rhrc).\n", hrc2));
                    m->pVBoxTranslator->release();
                    m->pVBoxTranslator = NULL;
                    m->pTrComponent = NULL;
                }
            }
            else
            {
                HRESULT hrc2 = Global::vboxStatusCodeToCOM(vrc);
                LogRel(("Path constructing failed (%Rhrc).\n", hrc2));
                m->pVBoxTranslator->release();
                m->pVBoxTranslator = NULL;
                m->pTrComponent = NULL;
            }
        }
        else
            LogRel(("Translator creation failed.\n"));
#endif

#ifdef VBOX_WITH_EXTPACK
        /*
         * Initialize extension pack manager before system properties because
         * it is required for the VD plugins.
         */
        hrc = unconst(m->ptrExtPackManager).createObject();
        if (SUCCEEDED(hrc))
            hrc = m->ptrExtPackManager->initExtPackManager(this, VBOXEXTPACKCTX_PER_USER_DAEMON);
        if (FAILED(hrc))
            throw hrc;
#endif
        /* guest OS type objects, needed by machines */
        for (size_t i = 0; i < Global::cOSTypes; ++i)
        {
            ComObjPtr<GuestOSType> guestOSTypeObj;
            hrc = guestOSTypeObj.createObject();
            if (SUCCEEDED(hrc))
            {
                hrc = guestOSTypeObj->init(Global::sOSTypes[i]);
                if (SUCCEEDED(hrc))
                    m->allGuestOSTypes.addChild(guestOSTypeObj);
            }
            ComAssertComRCThrowRC(hrc);
        }

        /* all registered media, needed by machines */
        if (FAILED(hrc = initMedia(m->uuidMediaRegistry,
                                  m->pMainConfigFile->mediaRegistry,
                                  Utf8Str::Empty)))     // const Utf8Str &machineFolder
            throw hrc;

        /* machines */
        if (FAILED(hrc = initMachines()))
            throw hrc;

#ifdef DEBUG
        LogFlowThisFunc(("Dumping media backreferences\n"));
        i_dumpAllBackRefs();
#endif

        /* net services - dhcp services */
        for (settings::DHCPServersList::const_iterator it = m->pMainConfigFile->llDhcpServers.begin();
             it != m->pMainConfigFile->llDhcpServers.end();
             ++it)
        {
            const settings::DHCPServer &data = *it;

            ComObjPtr<DHCPServer> pDhcpServer;
            if (SUCCEEDED(hrc = pDhcpServer.createObject()))
                hrc = pDhcpServer->init(this, data);
            if (FAILED(hrc)) throw hrc;

            hrc = i_registerDHCPServer(pDhcpServer, false /* aSaveRegistry */);
            if (FAILED(hrc)) throw hrc;
        }

        for (settings::SharedFoldersList::const_iterator it = m->pMainConfigFile->llGlobalSharedFolders.begin();
             it != m->pMainConfigFile->llGlobalSharedFolders.end();
             ++it)
        {
            const settings::SharedFolder &sf = *it;
            ComObjPtr<SharedFolder> pSharedFolder;
            hrc = pSharedFolder.createObject();
            AssertComRCThrowRC(hrc);
            hrc = pSharedFolder->init(this, sf);
            if (FAILED(hrc)) throw hrc;

            AutoWriteLock alock(m->allSharedFolders.getLockHandle() COMMA_LOCKVAL_SRC_POS);
            m->allSharedFolders.addChild(pSharedFolder);
            alock.release();
        }

        /* net services - nat networks */
        for (settings::NATNetworksList::const_iterator it = m->pMainConfigFile->llNATNetworks.begin();
             it != m->pMainConfigFile->llNATNetworks.end();
             ++it)
        {
            const settings::NATNetwork &net = *it;

            ComObjPtr<NATNetwork> pNATNetwork;
            hrc = pNATNetwork.createObject();
            AssertComRCThrowRC(hrc);
            hrc = pNATNetwork->init(this, "");
            AssertComRCThrowRC(hrc);
            hrc = pNATNetwork->i_loadSettings(net);
            AssertComRCThrowRC(hrc);
            hrc = i_registerNATNetwork(pNATNetwork, false /* aSaveRegistry */);
            AssertComRCThrowRC(hrc);
        }

#ifdef VBOX_WITH_VMNET
        /* host-only networks */
        for (settings::HostOnlyNetworksList::const_iterator it = m->pMainConfigFile->llHostOnlyNetworks.begin();
             it != m->pMainConfigFile->llHostOnlyNetworks.end();
             ++it)
        {
            ComObjPtr<HostOnlyNetwork> pHostOnlyNetwork;
            hrc = pHostOnlyNetwork.createObject();
            AssertComRCThrowRC(hrc);
            hrc = pHostOnlyNetwork->init(this, "TODO???");
            AssertComRCThrowRC(hrc);
            hrc = pHostOnlyNetwork->i_loadSettings(*it);
            AssertComRCThrowRC(hrc);
            m->allHostOnlyNetworks.addChild(pHostOnlyNetwork);
            AssertComRCThrowRC(hrc);
        }
#endif /* VBOX_WITH_VMNET */

#ifdef VBOX_WITH_CLOUD_NET
        /* net services - cloud networks */
        for (settings::CloudNetworksList::const_iterator it = m->pMainConfigFile->llCloudNetworks.begin();
             it != m->pMainConfigFile->llCloudNetworks.end();
             ++it)
        {
            ComObjPtr<CloudNetwork> pCloudNetwork;
            hrc = pCloudNetwork.createObject();
            AssertComRCThrowRC(hrc);
            hrc = pCloudNetwork->init(this, "");
            AssertComRCThrowRC(hrc);
            hrc = pCloudNetwork->i_loadSettings(*it);
            AssertComRCThrowRC(hrc);
            m->allCloudNetworks.addChild(pCloudNetwork);
            AssertComRCThrowRC(hrc);
        }
#endif /* VBOX_WITH_CLOUD_NET */

        /* cloud provider manager */
        hrc = unconst(m->pCloudProviderManager).createObject();
        if (SUCCEEDED(hrc))
            hrc = m->pCloudProviderManager->init(this);
        ComAssertComRCThrowRC(hrc);
        if (FAILED(hrc)) throw hrc;
    }
    catch (HRESULT err)
    {
        /* we assume that error info is set by the thrower */
        hrc = err;
    }
    catch (...)
    {
        hrc = VirtualBoxBase::handleUnexpectedExceptions(this, RT_SRC_POS);
    }

    if (SUCCEEDED(hrc))
    {
        /* set up client monitoring */
        try
        {
            unconst(m->pClientWatcher) = new ClientWatcher(this);
            if (!m->pClientWatcher->isReady())
            {
                delete m->pClientWatcher;
                unconst(m->pClientWatcher) = NULL;
                hrc = E_FAIL;
            }
        }
        catch (std::bad_alloc &)
        {
            hrc = E_OUTOFMEMORY;
        }
    }

    if (SUCCEEDED(hrc))
    {
        try
        {
            /* start the async event handler thread */
            int vrc = RTThreadCreate(&unconst(m->threadAsyncEvent),
                                     AsyncEventHandler,
                                     &unconst(m->pAsyncEventQ),
                                     0,
                                     RTTHREADTYPE_MAIN_WORKER,
                                     RTTHREADFLAGS_WAITABLE,
                                     "EventHandler");
            ComAssertRCThrow(vrc, E_FAIL);

            /* wait until the thread sets m->pAsyncEventQ */
            RTThreadUserWait(m->threadAsyncEvent, RT_INDEFINITE_WAIT);
            ComAssertThrow(m->pAsyncEventQ, E_FAIL);
        }
        catch (HRESULT hrcXcpt)
        {
            hrc = hrcXcpt;
        }
    }

#ifdef VBOX_WITH_EXTPACK
    /* Let the extension packs have a go at things. */
    if (SUCCEEDED(hrc))
    {
        lock.release();
        m->ptrExtPackManager->i_callAllVirtualBoxReadyHooks();
    }
#endif

    /* Confirm a successful initialization when it's the case. Must be last,
     * as on failure it will uninitialize the object. */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed(hrc);

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    LogFlow(("===========================================================\n"));
    /* Unconditionally return success, because the error return is delayed to
     * the attribute/method calls through the InitFailed object state. */
    return S_OK;
}

HRESULT VirtualBox::initMachines()
{
    for (settings::MachinesRegistry::const_iterator it = m->pMainConfigFile->llMachines.begin();
         it != m->pMainConfigFile->llMachines.end();
         ++it)
    {
        HRESULT hrc = S_OK;
        const settings::MachineRegistryEntry &xmlMachine = *it;
        Guid uuid = xmlMachine.uuid;

        /* Check if machine record has valid parameters. */
        if (xmlMachine.strSettingsFile.isEmpty() || uuid.isZero())
        {
            LogRel(("Skipped invalid machine record.\n"));
            continue;
        }

        ComObjPtr<Machine> pMachine;
        com::Utf8Str strPassword;
        if (SUCCEEDED(hrc = pMachine.createObject()))
        {
            hrc = pMachine->initFromSettings(this, xmlMachine.strSettingsFile, &uuid, strPassword);
            if (SUCCEEDED(hrc))
                hrc = i_registerMachine(pMachine);
            if (FAILED(hrc))
                return hrc;
        }
    }

    return S_OK;
}

/**
 * Loads a media registry from XML and adds the media contained therein to
 * the global lists of known media.
 *
 * This now (4.0) gets called from two locations:
 *
 *  --  VirtualBox::init(), to load the global media registry from VirtualBox.xml;
 *
 *  --  Machine::loadMachineDataFromSettings(), to load the per-machine registry
 *      from machine XML, for machines created with VirtualBox 4.0 or later.
 *
 * In both cases, the media found are added to the global lists so the
 * global arrays of media (including the GUI's virtual media manager)
 * continue to work as before.
 *
 * @param uuidRegistry The UUID of the media registry. This is either the
 *       transient UUID created at VirtualBox startup for the global registry or
 *       a machine ID.
 * @param mediaRegistry The XML settings structure to load, either from VirtualBox.xml
 *       or a machine XML.
 * @param strMachineFolder The folder of the machine.
 * @return
 */
HRESULT VirtualBox::initMedia(const Guid &uuidRegistry,
                              const settings::MediaRegistry &mediaRegistry,
                              const Utf8Str &strMachineFolder)
{
    LogFlow(("VirtualBox::initMedia ENTERING, uuidRegistry=%s, strMachineFolder=%s\n",
             uuidRegistry.toString().c_str(),
             strMachineFolder.c_str()));

    AutoWriteLock treeLock(i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    // the order of notification is critical for GUI, so use std::list<std::pair> instead of map
    std::list<std::pair<Guid, DeviceType_T> > uIdsForNotify;

    HRESULT hrc = S_OK;
    settings::MediaList::const_iterator it;
    for (it = mediaRegistry.llHardDisks.begin();
         it != mediaRegistry.llHardDisks.end();
         ++it)
    {
        const settings::Medium &xmlHD = *it;

        hrc = Medium::initFromSettings(this,
                                       DeviceType_HardDisk,
                                       uuidRegistry,
                                       strMachineFolder,
                                       xmlHD,
                                       treeLock,
                                       uIdsForNotify);
        if (FAILED(hrc)) return hrc;
    }

    for (it = mediaRegistry.llDvdImages.begin();
         it != mediaRegistry.llDvdImages.end();
         ++it)
    {
        const settings::Medium &xmlDvd = *it;

        hrc = Medium::initFromSettings(this,
                                       DeviceType_DVD,
                                       uuidRegistry,
                                       strMachineFolder,
                                       xmlDvd,
                                       treeLock,
                                       uIdsForNotify);
        if (FAILED(hrc)) return hrc;
    }

    for (it = mediaRegistry.llFloppyImages.begin();
         it != mediaRegistry.llFloppyImages.end();
         ++it)
    {
        const settings::Medium &xmlFloppy = *it;

        hrc = Medium::initFromSettings(this,
                                       DeviceType_Floppy,
                                       uuidRegistry,
                                       strMachineFolder,
                                       xmlFloppy,
                                       treeLock,
                                       uIdsForNotify);
        if (FAILED(hrc)) return hrc;
    }

    for (std::list<std::pair<Guid, DeviceType_T> >::const_iterator itItem = uIdsForNotify.begin();
         itItem != uIdsForNotify.end();
         ++itItem)
    {
        i_onMediumRegistered(itItem->first, itItem->second, TRUE);
    }

    LogFlow(("VirtualBox::initMedia LEAVING\n"));

    return S_OK;
}

void VirtualBox::uninit()
{
    /* Must be done outside the AutoUninitSpan, as it expects AutoCaller to
     * be successful. This needs additional checks to protect against double
     * uninit, as then the pointer is NULL. */
    if (RT_VALID_PTR(m))
    {
        Assert(!m->uRegistryNeedsSaving);
        if (m->uRegistryNeedsSaving)
            i_saveSettings();
    }

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlow(("===========================================================\n"));
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("initFailed()=%d\n", autoUninitSpan.initFailed()));

    /* tell all our child objects we've been uninitialized */

    LogFlowThisFunc(("Uninitializing machines (%d)...\n", m->allMachines.size()));
    if (m->pHost)
    {
        /* It is necessary to hold the VirtualBox and Host locks here because
           we may have to uninitialize SessionMachines. */
        AutoMultiWriteLock2 multilock(this, m->pHost COMMA_LOCKVAL_SRC_POS);
        m->allMachines.uninitAll();
    }
    else
        m->allMachines.uninitAll();

    m->allFloppyImages.uninitAll();
    m->allDVDImages.uninitAll();
    m->allHardDisks.uninitAll();
    m->allDHCPServers.uninitAll();
    m->allGuestOSTypes.uninitAll();

    /* Note that we release singleton children after we've all other children.
     * In some cases this is important because these other children may use
     * some resources of the singletons which would prevent them from
     * uninitializing (as for example, mSystemProperties which owns
     * MediumFormat objects which Medium objects refer to) */
    if (m->pCloudProviderManager)
    {
        m->pCloudProviderManager->uninit();
        unconst(m->pCloudProviderManager).setNull();
    }

    if (m->pSystemProperties)
    {
        m->pSystemProperties->uninit();
        unconst(m->pSystemProperties).setNull();
    }

    if (m->pHost)
    {
        m->pHost->uninit();
        unconst(m->pHost).setNull();
    }

#ifdef VBOX_WITH_RESOURCE_USAGE_API
    if (m->pPerformanceCollector)
    {
        m->pPerformanceCollector->uninit();
        unconst(m->pPerformanceCollector).setNull();
    }
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

    /*
     * Unload the cryptographic module if loaded before the extension
     * pack manager is torn down.
     */
    Assert(!m->cRefsCrypto);
    if (m->hLdrModCrypto != NIL_RTLDRMOD)
    {
        m->pCryptoIf = NULL;

        int vrc = RTLdrClose(m->hLdrModCrypto);
        AssertRC(vrc);
        m->hLdrModCrypto = NIL_RTLDRMOD;
    }

    RTCritSectDelete(&m->CritSectModCrypto);

    m->mapProgressOperations.clear();

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
    /*
     * Call gTrackedObjectsCollector uninitialization before ExtPackManager uninitialization!!!
     * Otherwise, this results in an error when releasing resources (in ComPtr::cleanup).
     */
    if (m->objectTrackerTask)
    {
        LogRel(("BACKEND: Terminating the object tracker...\n"));
        m->objectTrackerTask->finish();//set the termination flag in the thread
        delete m->objectTrackerTask;//waiting the thread termination is going in the m_objectTrackerTask destructor
        gTrackedObjectsCollector.uninit();
    }
#endif

#ifdef VBOX_WITH_EXTPACK
    if (m->ptrExtPackManager)
    {
        m->ptrExtPackManager->uninit();
        unconst(m->ptrExtPackManager).setNull();
    }
#endif

    LogFlowThisFunc(("Terminating the async event handler...\n"));
    if (m->threadAsyncEvent != NIL_RTTHREAD)
    {
        /* signal to exit the event loop */
        if (RT_SUCCESS(m->pAsyncEventQ->interruptEventQueueProcessing()))
        {
            /*
             *  Wait for thread termination (only after we've successfully
             *  interrupted the event queue processing!)
             */
            int vrc = RTThreadWait(m->threadAsyncEvent, 60000, NULL);
            if (RT_FAILURE(vrc))
                Log1WarningFunc(("RTThreadWait(%RTthrd) -> %Rrc\n", m->threadAsyncEvent, vrc));
        }
        else
        {
            AssertMsgFailed(("interruptEventQueueProcessing() failed\n"));
            RTThreadWait(m->threadAsyncEvent, 0, NULL);
        }

        unconst(m->threadAsyncEvent) = NIL_RTTHREAD;
        unconst(m->pAsyncEventQ) = NULL;
    }

    LogFlowThisFunc(("Releasing event source...\n"));
    if (m->pEventSource)
    {
        // Must uninit the event source here, because it makes no sense that
        // it survives longer than the base object. If someone gets an event
        // with such an event source then that's life and it has to be dealt
        // with appropriately on the API client side.
        m->pEventSource->uninit();
        unconst(m->pEventSource).setNull();
    }

    LogFlowThisFunc(("Terminating the client watcher...\n"));
    if (m->pClientWatcher)
    {
        delete m->pClientWatcher;
        unconst(m->pClientWatcher) = NULL;
    }

    delete m->pAutostartDb;
#ifdef VBOX_WITH_MAIN_NLS
    if (m->pVBoxTranslator)
        m->pVBoxTranslator->release();
#endif
    // clean up our instance data
    delete m;
    m = NULL;

    /* Unload hard disk plugin backends. */
    VDShutdown();

    LogFlowThisFuncLeave();
    LogFlow(("===========================================================\n"));
}

// Wrapped IVirtualBox properties
/////////////////////////////////////////////////////////////////////////////
HRESULT VirtualBox::getVersion(com::Utf8Str &aVersion)
{
    aVersion = sVersion;
    return S_OK;
}

HRESULT VirtualBox::getVersionNormalized(com::Utf8Str &aVersionNormalized)
{
    aVersionNormalized = sVersionNormalized;
    return S_OK;
}

HRESULT VirtualBox::getRevision(ULONG *aRevision)
{
    *aRevision = sRevision;
    return S_OK;
}

HRESULT VirtualBox::getPackageType(com::Utf8Str &aPackageType)
{
    aPackageType = sPackageType;
    return S_OK;
}

HRESULT VirtualBox::getAPIVersion(com::Utf8Str &aAPIVersion)
{
    aAPIVersion = sAPIVersion;
    return S_OK;
}

HRESULT VirtualBox::getAPIRevision(LONG64 *aAPIRevision)
{
    AssertCompile(VBOX_VERSION_MAJOR < 128 && VBOX_VERSION_MAJOR > 0);
    AssertCompile((uint64_t)VBOX_VERSION_MINOR < 256);
    uint64_t uRevision = ((uint64_t)VBOX_VERSION_MAJOR << 56)
                       | ((uint64_t)VBOX_VERSION_MINOR << 48)
                       | ((uint64_t)VBOX_VERSION_BUILD << 40);

    /** @todo This needs to be the same in OSE and non-OSE, preferrably
     *        only changing when actual API changes happens. */
    uRevision |= 1;

    *aAPIRevision = (LONG64)uRevision;

    return S_OK;
}

HRESULT VirtualBox::getHomeFolder(com::Utf8Str &aHomeFolder)
{
    /* mHomeDir is const and doesn't need a lock */
    aHomeFolder = m->strHomeDir;
    return S_OK;
}

HRESULT VirtualBox::getSettingsFilePath(com::Utf8Str &aSettingsFilePath)
{
    /* mCfgFile.mName is const and doesn't need a lock */
    aSettingsFilePath = m->strSettingsFilePath;
    return S_OK;
}

HRESULT VirtualBox::getHost(ComPtr<IHost> &aHost)
{
    /* mHost is const, no need to lock */
    m->pHost.queryInterfaceTo(aHost.asOutParam());
    return S_OK;
}

HRESULT VirtualBox::getPlatformProperties(PlatformArchitecture_T platformArchitecture,
                                          ComPtr<IPlatformProperties> &aPlatformProperties)
{
    ComObjPtr<PlatformProperties> platformProperties;
    HRESULT hrc = platformProperties.createObject();
    AssertComRCReturn(hrc, hrc);

    hrc = platformProperties->init(this);
    AssertComRCReturn(hrc, hrc);

    hrc = platformProperties->i_setArchitecture(platformArchitecture);
    AssertComRCReturn(hrc, hrc);

    return platformProperties.queryInterfaceTo(aPlatformProperties.asOutParam());
}

HRESULT VirtualBox::getSystemProperties(ComPtr<ISystemProperties> &aSystemProperties)
{
    /* mSystemProperties is const, no need to lock */
    m->pSystemProperties.queryInterfaceTo(aSystemProperties.asOutParam());
    return S_OK;
}

HRESULT VirtualBox::getMachines(std::vector<ComPtr<IMachine> > &aMachines)
{
    AutoReadLock al(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aMachines.resize(m->allMachines.size());
    size_t i = 0;
    for (MachinesOList::const_iterator it= m->allMachines.begin();
         it!= m->allMachines.end(); ++it, ++i)
        (*it).queryInterfaceTo(aMachines[i].asOutParam());
    return S_OK;
}

HRESULT VirtualBox::getMachineGroups(std::vector<com::Utf8Str> &aMachineGroups)
{
    std::list<com::Utf8Str> allGroups;

    /* get copy of all machine references, to avoid holding the list lock */
    MachinesOList::MyList allMachines;
    {
        AutoReadLock al(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);
        allMachines = m->allMachines.getList();
    }
    for (MachinesOList::MyList::const_iterator it = allMachines.begin();
         it != allMachines.end();
         ++it)
    {
        const ComObjPtr<Machine> &pMachine = *it;
        AutoCaller autoMachineCaller(pMachine);
        if (FAILED(autoMachineCaller.hrc()))
            continue;
        AutoReadLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);

        if (pMachine->i_isAccessible())
        {
            const StringsList &thisGroups = pMachine->i_getGroups();
            for (StringsList::const_iterator it2 = thisGroups.begin();
                 it2 != thisGroups.end(); ++it2)
                allGroups.push_back(*it2);
        }
    }

    /* throw out any duplicates */
    allGroups.sort();
    allGroups.unique();
    aMachineGroups.resize(allGroups.size());
    size_t i = 0;
    for (std::list<com::Utf8Str>::const_iterator it = allGroups.begin();
         it != allGroups.end(); ++it, ++i)
        aMachineGroups[i] = (*it);
    return S_OK;
}

HRESULT VirtualBox::getHardDisks(std::vector<ComPtr<IMedium> > &aHardDisks)
{
    AutoReadLock al(m->allHardDisks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aHardDisks.resize(m->allHardDisks.size());
    size_t i = 0;
    for (MediaOList::const_iterator it = m->allHardDisks.begin();
         it !=  m->allHardDisks.end(); ++it, ++i)
        (*it).queryInterfaceTo(aHardDisks[i].asOutParam());
    return S_OK;
}

HRESULT VirtualBox::getDVDImages(std::vector<ComPtr<IMedium> > &aDVDImages)
{
    AutoReadLock al(m->allDVDImages.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aDVDImages.resize(m->allDVDImages.size());
    size_t i = 0;
    for (MediaOList::const_iterator it = m->allDVDImages.begin();
         it!= m->allDVDImages.end(); ++it, ++i)
        (*it).queryInterfaceTo(aDVDImages[i].asOutParam());
    return S_OK;
}

HRESULT VirtualBox::getFloppyImages(std::vector<ComPtr<IMedium> > &aFloppyImages)
{
    AutoReadLock al(m->allFloppyImages.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aFloppyImages.resize(m->allFloppyImages.size());
    size_t i = 0;
    for (MediaOList::const_iterator it = m->allFloppyImages.begin();
         it != m->allFloppyImages.end(); ++it, ++i)
        (*it).queryInterfaceTo(aFloppyImages[i].asOutParam());
    return S_OK;
}

HRESULT VirtualBox::getProgressOperations(std::vector<ComPtr<IProgress> > &aProgressOperations)
{
    /* protect mProgressOperations */
    AutoReadLock safeLock(m->mtxProgressOperations COMMA_LOCKVAL_SRC_POS);
    ProgressMap pmap(m->mapProgressOperations);
    /* Can release lock now. The following code works on a copy of the map. */
    safeLock.release();
    aProgressOperations.resize(pmap.size());
    size_t i = 0;
    for (ProgressMap::iterator it = pmap.begin(); it != pmap.end(); ++it, ++i)
        it->second.queryInterfaceTo(aProgressOperations[i].asOutParam());
    return S_OK;
}


/**
 * Returns all supported guest OS types for one ore more platform architecture(s).
 *
 * @returns HRESULT
 * @param   aArchitectures          Platform architectures to return supported guest OS types for.
 *                                  If empty, all supported guest OS for all platform architectures will be returned.
 * @param   aGuestOSTypes           Where to return the supported guest OS types.
 *                                  Will be empty if none supported.
 */
HRESULT VirtualBox::i_getSupportedGuestOSTypes(std::vector<PlatformArchitecture_T> aArchitectures,
                                               std::vector<ComPtr<IGuestOSType> > &aGuestOSTypes)
{
    AutoReadLock al(m->allGuestOSTypes.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    aGuestOSTypes.clear();

    /** @todo We might want to redo this at some point, to better group / hash this. */

    for (GuestOSTypesOList::const_iterator it = m->allGuestOSTypes.begin(); it != m->allGuestOSTypes.end(); ++it)
    {
        bool fFound = false;
        if (aArchitectures.size() == 0) /* If empty, we add all types we have. */
            fFound = true;
        else
        {
            for (size_t i = 0; i < aArchitectures.size(); i++)
            {
                if (aArchitectures[i] == (*it)->i_platformArchitecture())
                {
                    fFound = true;
                    break;
                }
            }
        }

        if (fFound)
        {
            ComPtr<IGuestOSType> osType;
            (*it).queryInterfaceTo(osType.asOutParam());
            aGuestOSTypes.push_back(osType);
        }
    }

    return S_OK;
}

HRESULT VirtualBox::getGuestOSTypes(std::vector<ComPtr<IGuestOSType> > &aGuestOSTypes)
{
    std::vector<PlatformArchitecture_T> vecArchitectures; /* Stays empty to return all guest OS types. */
    return VirtualBox::i_getSupportedGuestOSTypes(vecArchitectures, aGuestOSTypes);
}

HRESULT VirtualBox::getSharedFolders(std::vector<ComPtr<ISharedFolder> > &aSharedFolders)
{
    AutoReadLock al(m->allSharedFolders.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aSharedFolders.resize(m->allSharedFolders.size());
    size_t i = 0;
    for (SharedFoldersOList::const_iterator it= m->allSharedFolders.begin();
         it!= m->allSharedFolders.end(); ++it, ++i)
         (*it).queryInterfaceTo(aSharedFolders[i].asOutParam());
    return S_OK;
}

HRESULT VirtualBox::getPerformanceCollector(ComPtr<IPerformanceCollector> &aPerformanceCollector)
{
#ifdef VBOX_WITH_RESOURCE_USAGE_API
    /* mPerformanceCollector is const, no need to lock */
    m->pPerformanceCollector.queryInterfaceTo(aPerformanceCollector.asOutParam());

    return S_OK;
#else /* !VBOX_WITH_RESOURCE_USAGE_API */
    NOREF(aPerformanceCollector);
    ReturnComNotImplemented();
#endif /* !VBOX_WITH_RESOURCE_USAGE_API */
}

HRESULT VirtualBox::getDHCPServers(std::vector<ComPtr<IDHCPServer> > &aDHCPServers)
{
    AutoReadLock al(m->allDHCPServers.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aDHCPServers.resize(m->allDHCPServers.size());
    size_t i = 0;
    for (DHCPServersOList::const_iterator it= m->allDHCPServers.begin();
         it!= m->allDHCPServers.end(); ++it, ++i)
         (*it).queryInterfaceTo(aDHCPServers[i].asOutParam());
    return S_OK;
}


HRESULT VirtualBox::getNATNetworks(std::vector<ComPtr<INATNetwork> > &aNATNetworks)
{
#ifdef VBOX_WITH_NAT_SERVICE
    AutoReadLock al(m->allNATNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aNATNetworks.resize(m->allNATNetworks.size());
    size_t i = 0;
    for (NATNetworksOList::const_iterator it= m->allNATNetworks.begin();
         it!= m->allNATNetworks.end(); ++it, ++i)
         (*it).queryInterfaceTo(aNATNetworks[i].asOutParam());
    return S_OK;
#else
    NOREF(aNATNetworks);
    return E_NOTIMPL;
#endif
}

HRESULT VirtualBox::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    /* event source is const, no need to lock */
    m->pEventSource.queryInterfaceTo(aEventSource.asOutParam());
    return S_OK;
}

HRESULT VirtualBox::getExtensionPackManager(ComPtr<IExtPackManager> &aExtensionPackManager)
{
    HRESULT hrc = S_OK;
#ifdef VBOX_WITH_EXTPACK
    /* The extension pack manager is const, no need to lock. */
    hrc = m->ptrExtPackManager.queryInterfaceTo(aExtensionPackManager.asOutParam());
#else
    hrc = E_NOTIMPL;
    NOREF(aExtensionPackManager);
#endif
    return hrc;
}

/**
 * Host Only Network
 */
HRESULT VirtualBox::createHostOnlyNetwork(const com::Utf8Str &aNetworkName,
                                          ComPtr<IHostOnlyNetwork> &aNetwork)
{
#ifdef VBOX_WITH_VMNET
    ComObjPtr<HostOnlyNetwork> HostOnlyNetwork;
    HostOnlyNetwork.createObject();
    HRESULT hrc = HostOnlyNetwork->init(this, aNetworkName);
    if (FAILED(hrc)) return hrc;

    m->allHostOnlyNetworks.addChild(HostOnlyNetwork);

    {
        AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
        vboxLock.release();

        if (FAILED(hrc))
            m->allHostOnlyNetworks.removeChild(HostOnlyNetwork);
        else
            HostOnlyNetwork.queryInterfaceTo(aNetwork.asOutParam());
    }

    return hrc;
#else /* !VBOX_WITH_VMNET */
    NOREF(aNetworkName);
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_VMNET */
}

HRESULT VirtualBox::findHostOnlyNetworkByName(const com::Utf8Str &aNetworkName,
                                           ComPtr<IHostOnlyNetwork> &aNetwork)
{
#ifdef VBOX_WITH_VMNET
    Bstr bstrNameToFind(aNetworkName);

    AutoReadLock alock(m->allHostOnlyNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (HostOnlyNetworksOList::const_iterator it = m->allHostOnlyNetworks.begin();
         it != m->allHostOnlyNetworks.end();
         ++it)
    {
        Bstr bstrHostOnlyNetworkName;
        HRESULT hrc = (*it)->COMGETTER(NetworkName)(bstrHostOnlyNetworkName.asOutParam());
        if (FAILED(hrc)) return hrc;

        if (bstrHostOnlyNetworkName == bstrNameToFind)
        {
            it->queryInterfaceTo(aNetwork.asOutParam());
            return S_OK;
        }
    }
    return VBOX_E_OBJECT_NOT_FOUND;
#else /* !VBOX_WITH_VMNET */
    NOREF(aNetworkName);
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_VMNET */
}

HRESULT VirtualBox::findHostOnlyNetworkById(const com::Guid &aId,
                                           ComPtr<IHostOnlyNetwork> &aNetwork)
{
#ifdef VBOX_WITH_VMNET
    ComObjPtr<HostOnlyNetwork> network;
    AutoReadLock alock(m->allHostOnlyNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (HostOnlyNetworksOList::const_iterator it = m->allHostOnlyNetworks.begin();
         it != m->allHostOnlyNetworks.end();
         ++it)
    {
        Bstr bstrHostOnlyNetworkId;
        HRESULT hrc = (*it)->COMGETTER(Id)(bstrHostOnlyNetworkId.asOutParam());
        if (FAILED(hrc)) return hrc;

        if (Guid(bstrHostOnlyNetworkId) == aId)
        {
            it->queryInterfaceTo(aNetwork.asOutParam());;
            return S_OK;
        }
    }
    return VBOX_E_OBJECT_NOT_FOUND;
#else /* !VBOX_WITH_VMNET */
    NOREF(aId);
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_VMNET */
}

HRESULT VirtualBox::removeHostOnlyNetwork(const ComPtr<IHostOnlyNetwork> &aNetwork)
{
#ifdef VBOX_WITH_VMNET
    Bstr name;
    HRESULT hrc = aNetwork->COMGETTER(NetworkName)(name.asOutParam());
    if (FAILED(hrc))
        return hrc;
    IHostOnlyNetwork *p = aNetwork;
    HostOnlyNetwork *network = static_cast<HostOnlyNetwork *>(p);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoCaller HostOnlyNetworkCaller(network);
    AssertComRCReturnRC(HostOnlyNetworkCaller.hrc());

    m->allHostOnlyNetworks.removeChild(network);

    {
        AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
        vboxLock.release();

        if (FAILED(hrc))
            m->allHostOnlyNetworks.addChild(network);
    }
    return hrc;
#else /* !VBOX_WITH_VMNET */
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_VMNET */
}

HRESULT VirtualBox::getHostOnlyNetworks(std::vector<ComPtr<IHostOnlyNetwork> > &aHostOnlyNetworks)
{
#ifdef VBOX_WITH_VMNET
    AutoReadLock al(m->allHostOnlyNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aHostOnlyNetworks.resize(m->allHostOnlyNetworks.size());
    size_t i = 0;
    for (HostOnlyNetworksOList::const_iterator it = m->allHostOnlyNetworks.begin();
         it != m->allHostOnlyNetworks.end(); ++it)
         (*it).queryInterfaceTo(aHostOnlyNetworks[i++].asOutParam());
    return S_OK;
#else /* !VBOX_WITH_VMNET */
    NOREF(aHostOnlyNetworks);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_VMNET */
}


HRESULT VirtualBox::getInternalNetworks(std::vector<com::Utf8Str> &aInternalNetworks)
{
    std::list<com::Utf8Str> allInternalNetworks;

    /* get copy of all machine references, to avoid holding the list lock */
    MachinesOList::MyList allMachines;
    {
        AutoReadLock al(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);
        allMachines = m->allMachines.getList();
    }
    for (MachinesOList::MyList::const_iterator it = allMachines.begin();
         it != allMachines.end(); ++it)
    {
        const ComObjPtr<Machine> &pMachine = *it;
        AutoCaller autoMachineCaller(pMachine);
        if (FAILED(autoMachineCaller.hrc()))
            continue;
        AutoReadLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);

        if (pMachine->i_isAccessible())
        {
            ChipsetType_T enmChipsetType;
            HRESULT hrc = pMachine->i_getPlatform()->getChipsetType(&enmChipsetType);
            ComAssertComRC(hrc);

            uint32_t const cNetworkAdapters = PlatformProperties::s_getMaxNetworkAdapters(enmChipsetType);
            for (ULONG i = 0; i < cNetworkAdapters; i++)
            {
                ComPtr<INetworkAdapter> pNet;
                hrc = pMachine->GetNetworkAdapter(i, pNet.asOutParam());
                if (FAILED(hrc) || pNet.isNull())
                    continue;
                Bstr strInternalNetwork;
                hrc = pNet->COMGETTER(InternalNetwork)(strInternalNetwork.asOutParam());
                if (FAILED(hrc) || strInternalNetwork.isEmpty())
                    continue;

                allInternalNetworks.push_back(Utf8Str(strInternalNetwork));
            }
        }
    }

    /* throw out any duplicates */
    allInternalNetworks.sort();
    allInternalNetworks.unique();
    size_t i = 0;
    aInternalNetworks.resize(allInternalNetworks.size());
    for (std::list<com::Utf8Str>::const_iterator it = allInternalNetworks.begin();
         it != allInternalNetworks.end();
         ++it, ++i)
        aInternalNetworks[i] = *it;
    return S_OK;
}

HRESULT VirtualBox::getGenericNetworkDrivers(std::vector<com::Utf8Str> &aGenericNetworkDrivers)
{
    std::list<com::Utf8Str> allGenericNetworkDrivers;

    /* get copy of all machine references, to avoid holding the list lock */
    MachinesOList::MyList allMachines;
    {
        AutoReadLock al(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);
        allMachines = m->allMachines.getList();
    }
    for (MachinesOList::MyList::const_iterator it = allMachines.begin();
         it != allMachines.end();
         ++it)
    {
        const ComObjPtr<Machine> &pMachine = *it;
        AutoCaller autoMachineCaller(pMachine);
        if (FAILED(autoMachineCaller.hrc()))
            continue;
        AutoReadLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);

        if (pMachine->i_isAccessible())
        {
            ChipsetType_T enmChipsetType;
            HRESULT hrc = pMachine->i_getPlatform()->getChipsetType(&enmChipsetType);
            ComAssertComRC(hrc);

            uint32_t const cNetworkAdapters = PlatformProperties::s_getMaxNetworkAdapters(enmChipsetType);
            for (ULONG i = 0; i < cNetworkAdapters; i++)
            {
                ComPtr<INetworkAdapter> pNet;
                hrc = pMachine->GetNetworkAdapter(i, pNet.asOutParam());
                if (FAILED(hrc) || pNet.isNull())
                    continue;
                Bstr strGenericNetworkDriver;
                hrc = pNet->COMGETTER(GenericDriver)(strGenericNetworkDriver.asOutParam());
                if (FAILED(hrc) || strGenericNetworkDriver.isEmpty())
                    continue;

                allGenericNetworkDrivers.push_back(Utf8Str(strGenericNetworkDriver).c_str());
            }
        }
    }

    /* throw out any duplicates */
    allGenericNetworkDrivers.sort();
    allGenericNetworkDrivers.unique();
    aGenericNetworkDrivers.resize(allGenericNetworkDrivers.size());
    size_t i = 0;
    for (std::list<com::Utf8Str>::const_iterator it = allGenericNetworkDrivers.begin();
         it != allGenericNetworkDrivers.end(); ++it, ++i)
        aGenericNetworkDrivers[i] = *it;

    return S_OK;
}

/**
 * Cloud Network
 */
#ifdef VBOX_WITH_CLOUD_NET
HRESULT VirtualBox::i_findCloudNetworkByName(const com::Utf8Str &aNetworkName,
                                             ComObjPtr<CloudNetwork> *aNetwork)
{
    ComPtr<CloudNetwork> found;
    Bstr bstrNameToFind(aNetworkName);

    AutoReadLock alock(m->allCloudNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (CloudNetworksOList::const_iterator it = m->allCloudNetworks.begin();
         it != m->allCloudNetworks.end();
         ++it)
    {
        Bstr bstrCloudNetworkName;
        HRESULT hrc = (*it)->COMGETTER(NetworkName)(bstrCloudNetworkName.asOutParam());
        if (FAILED(hrc)) return hrc;

        if (bstrCloudNetworkName == bstrNameToFind)
        {
            *aNetwork = *it;
            return S_OK;
        }
    }
    return VBOX_E_OBJECT_NOT_FOUND;
}
#endif /* VBOX_WITH_CLOUD_NET */

HRESULT VirtualBox::createCloudNetwork(const com::Utf8Str &aNetworkName,
                                       ComPtr<ICloudNetwork> &aNetwork)
{
#ifdef VBOX_WITH_CLOUD_NET
    ComObjPtr<CloudNetwork> cloudNetwork;
    cloudNetwork.createObject();
    HRESULT hrc = cloudNetwork->init(this, aNetworkName);
    if (FAILED(hrc)) return hrc;

    m->allCloudNetworks.addChild(cloudNetwork);

    {
        AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
        vboxLock.release();

        if (FAILED(hrc))
            m->allCloudNetworks.removeChild(cloudNetwork);
        else
            cloudNetwork.queryInterfaceTo(aNetwork.asOutParam());
    }

    return hrc;
#else /* !VBOX_WITH_CLOUD_NET */
    NOREF(aNetworkName);
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_CLOUD_NET */
}

HRESULT VirtualBox::findCloudNetworkByName(const com::Utf8Str &aNetworkName,
                                           ComPtr<ICloudNetwork> &aNetwork)
{
#ifdef VBOX_WITH_CLOUD_NET
    ComObjPtr<CloudNetwork> network;
    HRESULT hrc = i_findCloudNetworkByName(aNetworkName, &network);
    if (SUCCEEDED(hrc))
        network.queryInterfaceTo(aNetwork.asOutParam());
    return hrc;
#else /* !VBOX_WITH_CLOUD_NET */
    NOREF(aNetworkName);
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_CLOUD_NET */
}

HRESULT VirtualBox::removeCloudNetwork(const ComPtr<ICloudNetwork> &aNetwork)
{
#ifdef VBOX_WITH_CLOUD_NET
    Bstr name;
    HRESULT hrc = aNetwork->COMGETTER(NetworkName)(name.asOutParam());
    if (FAILED(hrc))
        return hrc;
    ICloudNetwork *p = aNetwork;
    CloudNetwork *network = static_cast<CloudNetwork *>(p);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoCaller cloudNetworkCaller(network);
    AssertComRCReturnRC(cloudNetworkCaller.hrc());

    m->allCloudNetworks.removeChild(network);

    {
        AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
        vboxLock.release();

        if (FAILED(hrc))
            m->allCloudNetworks.addChild(network);
    }
    return hrc;
#else /* !VBOX_WITH_CLOUD_NET */
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_CLOUD_NET */
}

HRESULT VirtualBox::getCloudNetworks(std::vector<ComPtr<ICloudNetwork> > &aCloudNetworks)
{
#ifdef VBOX_WITH_CLOUD_NET
    AutoReadLock al(m->allCloudNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    aCloudNetworks.resize(m->allCloudNetworks.size());
    size_t i = 0;
    for (CloudNetworksOList::const_iterator it = m->allCloudNetworks.begin();
         it != m->allCloudNetworks.end(); ++it)
         (*it).queryInterfaceTo(aCloudNetworks[i++].asOutParam());
    return S_OK;
#else /* !VBOX_WITH_CLOUD_NET */
    NOREF(aCloudNetworks);
    return E_NOTIMPL;
#endif /* !VBOX_WITH_CLOUD_NET */
}

#ifdef VBOX_WITH_CLOUD_NET
HRESULT VirtualBox::i_getEventSource(ComPtr<IEventSource>& aSource)
{
    m->pEventSource.queryInterfaceTo(aSource.asOutParam());
    return S_OK;
}
#endif /* VBOX_WITH_CLOUD_NET */

HRESULT VirtualBox::getCloudProviderManager(ComPtr<ICloudProviderManager> &aCloudProviderManager)
{
    HRESULT hrc = m->pCloudProviderManager.queryInterfaceTo(aCloudProviderManager.asOutParam());
    return hrc;
}

HRESULT VirtualBox::checkFirmwarePresent(PlatformArchitecture_T aPlatformArchitecture,
                                         FirmwareType_T aFirmwareType,
                                         const com::Utf8Str &aVersion,
                                         com::Utf8Str &aUrl,
                                         com::Utf8Str &aFile,
                                         BOOL *aResult)
{
    NOREF(aVersion);

    static const VBOXFWDESC s_aFwDescX86[] =
    {
        {   FirmwareType_BIOS,    true,  NULL,               NULL },
#ifdef VBOX_WITH_EFI_IN_DD2
        {   FirmwareType_EFI32,   true,  "VBoxEFI-x86.fd",   NULL },
        {   FirmwareType_EFI64,   true,  "VBoxEFI-amd64.fd", NULL },
        {   FirmwareType_EFIDUAL, true,  "VBoxEFIDual.fd",   NULL },
#else                                                         /* Note! These links does not work! */
        {   FirmwareType_EFI32,   false, "VBoxEFI-x86.fd",   "http://virtualbox.org/firmware/VBoxEFI-x86.fd" },
        {   FirmwareType_EFI64,   false, "VBoxEFI-amd64.fd", "http://virtualbox.org/firmware/VBoxEFI-amd64.fd" },
        {   FirmwareType_EFIDUAL, false, "VBoxEFIDual.fd",   "http://virtualbox.org/firmware/VBoxEFIDual.fd" },
#endif
    };

    static const VBOXFWDESC s_aFwDescArm[] =
    {
#ifdef VBOX_WITH_EFI_IN_DD2
        {   FirmwareType_EFI32,   true,  "VBoxEFI-arm32.fd", NULL },
        {   FirmwareType_EFI64,   true,  "VBoxEFI-arm64.fd", NULL },
 #else                                                        /* Note! These links does not work! */
        {   FirmwareType_EFI32,   false, "VBoxEFI-arm32.fd", "http://virtualbox.org/firmware/VBoxEFI-arm32.fd" },
        {   FirmwareType_EFI64,   false, "VBoxEFI-arm64.fd", "http://virtualbox.org/firmware/VBoxEFI-arm64.fd" },
#endif
    };

    PVBOXFWDESC pFwDesc = NULL;
    uint32_t cFwDesc = 0;
    if (aPlatformArchitecture == PlatformArchitecture_x86)
    {
        pFwDesc = &s_aFwDescX86[0];
        cFwDesc = RT_ELEMENTS(s_aFwDescX86);
    }
    else if (aPlatformArchitecture == PlatformArchitecture_ARM)
    {
        pFwDesc = &s_aFwDescArm[0];
        cFwDesc = RT_ELEMENTS(s_aFwDescArm);
    }
    else
        return E_INVALIDARG;

    for (size_t i = 0; i < cFwDesc; i++)
    {
        if (aFirmwareType != pFwDesc->enmType)
        {
            pFwDesc++;
            continue;
        }

        /* compiled-in firmware */
        if (pFwDesc->fBuiltIn)
        {
            aFile = pFwDesc->pszFileName;
            *aResult = TRUE;
            break;
        }

        Utf8Str    fullName;
        Utf8StrFmt shortName("Firmware%c%s", RTPATH_DELIMITER, pFwDesc->pszFileName);
        int vrc = i_calculateFullPath(shortName, fullName);
        AssertRCReturn(vrc, VBOX_E_IPRT_ERROR);
        if (RTFileExists(fullName.c_str()))
        {
            *aResult = TRUE;
            aFile = fullName;
            break;
        }

        char szVBoxPath[RTPATH_MAX];
        vrc = RTPathExecDir(szVBoxPath, RTPATH_MAX);
        AssertRCReturn(vrc, VBOX_E_IPRT_ERROR);
        vrc = RTPathAppend(szVBoxPath, sizeof(szVBoxPath), pFwDesc->pszFileName);
        AssertRCReturn(vrc, VBOX_E_IPRT_ERROR);
        if (RTFileExists(szVBoxPath))
        {
            *aResult = TRUE;
            aFile = szVBoxPath;
            break;
        }

        /** @todo account for version in the URL */
        aUrl = pFwDesc->pszUrl;
        *aResult = FALSE;

        /* Assume single record per firmware type */
        break;
    }

    return S_OK;
}

/**
 * Walk the list of GuestOSType objects and return a list of all known guest
 * OS families.
 *
 * @param aOSFamilies    Where to store the list of guest OS families.
 *
 * @note Locks the guest OS types list for reading.
 */
HRESULT VirtualBox::getGuestOSFamilies(std::vector<com::Utf8Str> &aOSFamilies)
{
    std::list<com::Utf8Str> allOSFamilies;

    AutoReadLock alock(m->allGuestOSTypes.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (GuestOSTypesOList::const_iterator it = m->allGuestOSTypes.begin();
         it != m->allGuestOSTypes.end(); ++it)
    {
        const Utf8Str &familyId = (*it)->i_familyId();
        AssertMsg(!familyId.isEmpty(), ("familyId must not be NULL"));
        allOSFamilies.push_back(familyId);
    }

    /* throw out any duplicates */
    allOSFamilies.sort();
    allOSFamilies.unique();

    aOSFamilies.resize(allOSFamilies.size());
    size_t i = 0;
    for (std::list<com::Utf8Str>::const_iterator it = allOSFamilies.begin();
         it != allOSFamilies.end(); ++it, ++i)
        aOSFamilies[i] = (*it);

    return S_OK;
}

// Wrapped IVirtualBox methods
/////////////////////////////////////////////////////////////////////////////

/* Helper for VirtualBox::ComposeMachineFilename */
static void sanitiseMachineFilename(Utf8Str &aName);

HRESULT VirtualBox::composeMachineFilename(const com::Utf8Str &aName,
                                           const com::Utf8Str &aGroup,
                                           const com::Utf8Str &aCreateFlags,
                                           const com::Utf8Str &aBaseFolder,
                                           com::Utf8Str       &aFile)
{
    if (RT_UNLIKELY(aName.isEmpty()))
        return setError(E_INVALIDARG, tr("Machine name is invalid, must not be empty"));

    Utf8Str strBase = aBaseFolder;
    Utf8Str strName = aName;

    LogFlowThisFunc(("aName=\"%s\",aBaseFolder=\"%s\"\n", strName.c_str(), strBase.c_str()));

    com::Guid id;
    bool fDirectoryIncludesUUID = false;
    if (!aCreateFlags.isEmpty())
    {
        size_t uPos = 0;
        com::Utf8Str strKey;
        com::Utf8Str strValue;
        while ((uPos = aCreateFlags.parseKeyValue(strKey, strValue, uPos)) != com::Utf8Str::npos)
        {
            if (strKey == "UUID")
                id = strValue.c_str();
            else if (strKey == "directoryIncludesUUID")
                fDirectoryIncludesUUID = (strValue == "1");
        }
    }

    if (id.isZero())
        fDirectoryIncludesUUID = false;
    else if (!id.isValid())
    {
        /* do something else */
        return setError(E_INVALIDARG,
                 tr("'%s' is not a valid Guid"),
                 id.toStringCurly().c_str());
    }

    Utf8Str strGroup(aGroup);
    if (strGroup.isEmpty())
        strGroup = "/";
    HRESULT hrc = i_validateMachineGroup(strGroup, true);
    if (FAILED(hrc))
        return hrc;

    /* Compose the settings file name using the following scheme:
     *
     *     <base_folder><group>/<machine_name>/<machine_name>.xml
     *
     * If a non-null and non-empty base folder is specified, the default
     * machine folder will be used as a base folder.
     * We sanitise the machine name to a safe white list of characters before
     * using it.
     */
    Utf8Str strDirName(strName);
    if (fDirectoryIncludesUUID)
        strDirName += Utf8StrFmt(" (%RTuuid)", id.raw());
    sanitiseMachineFilename(strName);
    sanitiseMachineFilename(strDirName);

    if (strBase.isEmpty())
        /* we use the non-full folder value below to keep the path relative */
        i_getDefaultMachineFolder(strBase);

    i_calculateFullPath(strBase, strBase);

    /* eliminate toplevel group to avoid // in the result */
    if (strGroup == "/")
        strGroup.setNull();
    aFile = com::Utf8StrFmt("%s%s%c%s%c%s.vbox",
                            strBase.c_str(),
                            strGroup.c_str(),
                            RTPATH_DELIMITER,
                            strDirName.c_str(),
                            RTPATH_DELIMITER,
                            strName.c_str());
    return S_OK;
}

/**
 * Remove characters from a machine file name which can be problematic on
 * particular systems.
 * @param  strName  The file name to sanitise.
 */
void sanitiseMachineFilename(Utf8Str &strName)
{
    if (strName.isEmpty())
        return;

    /* Set of characters which should be safe for use in filenames: some basic
     * ASCII, Unicode from Latin-1 alphabetic to the end of Hangul.  We try to
     * skip anything that could count as a control character in Windows or
     * *nix, or be otherwise difficult for shells to handle (I would have
     * preferred to remove the space and brackets too).  We also remove all
     * characters which need UTF-16 surrogate pairs for Windows's benefit.
     */
    static RTUNICP const s_uszValidRangePairs[] =
    {
        ' ', ' ',
        '(', ')',
        '-', '.',
        '0', '9',
        'A', 'Z',
        'a', 'z',
        '_', '_',
        0xa0, 0xd7af,
        '\0'
    };

    char *pszName = strName.mutableRaw();
    ssize_t cReplacements = RTStrPurgeComplementSet(pszName, s_uszValidRangePairs, '_');
    Assert(cReplacements >= 0);
    NOREF(cReplacements);

    /* No leading dot or dash. */
    if (pszName[0] == '.' || pszName[0] == '-')
        pszName[0] = '_';

    /* No trailing dot. */
    if (pszName[strName.length() - 1] == '.')
        pszName[strName.length() - 1] = '_';

    /* Mangle leading and trailing spaces. */
    for (size_t i = 0; pszName[i] == ' '; ++i)
        pszName[i] = '_';
    for (size_t i = strName.length() - 1; i && pszName[i] == ' '; --i)
        pszName[i] = '_';
}

#ifdef DEBUG
typedef DECLCALLBACKTYPE(void, FNTESTPRINTF,(const char *, ...));
/** Simple unit test/operation examples for sanitiseMachineFilename(). */
static unsigned testSanitiseMachineFilename(FNTESTPRINTF *pfnPrintf)
{
    unsigned cErrors = 0;

    /** Expected results of sanitising given file names. */
    static struct
    {
        /** The test file name to be sanitised (Utf-8). */
        const char *pcszIn;
        /** The expected sanitised output (Utf-8). */
        const char *pcszOutExpected;
    } aTest[] =
    {
        { "OS/2 2.1", "OS_2 2.1" },
        { "-!My VM!-", "__My VM_-" },
        { "\xF0\x90\x8C\xB0", "____" },
        { "  My VM  ", "__My VM__" },
        { ".My VM.", "_My VM_" },
        { "My VM", "My VM" }
    };
    for (unsigned i = 0; i < RT_ELEMENTS(aTest); ++i)
    {
        Utf8Str str(aTest[i].pcszIn);
        sanitiseMachineFilename(str);
        if (str.compare(aTest[i].pcszOutExpected))
        {
            ++cErrors;
            pfnPrintf("%s: line %d, expected %s, actual %s\n",
                      __PRETTY_FUNCTION__, i, aTest[i].pcszOutExpected,
                      str.c_str());
        }
    }
    return cErrors;
}

/** @todo Proper testcase. */
/** @todo Do we have a better method of doing init functions? */
namespace
{
    class TestSanitiseMachineFilename
    {
    public:
        TestSanitiseMachineFilename(void)
        {
            Assert(!testSanitiseMachineFilename(RTAssertMsg2));
        }
    };
    TestSanitiseMachineFilename s_TestSanitiseMachineFilename;
}
#endif

/** @note Locks mSystemProperties object for reading. */
HRESULT VirtualBox::createMachine(const com::Utf8Str &aSettingsFile,
                                  const com::Utf8Str &aName,
                                  PlatformArchitecture_T aArchitecture,
                                  const std::vector<com::Utf8Str> &aGroups,
                                  const com::Utf8Str &aOsTypeId,
                                  const com::Utf8Str &aFlags,
                                  const com::Utf8Str &aCipher,
                                  const com::Utf8Str &aPasswordId,
                                  const com::Utf8Str &aPassword,
                                  ComPtr<IMachine> &aMachine)
{
    if (aArchitecture == PlatformArchitecture_None)
        return setError(E_INVALIDARG, tr("'Must specify a valid platform architecture"));

    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aSettingsFile=\"%s\", aName=\"%s\", aArchitecture=%#x, aOsTypeId =\"%s\", aCreateFlags=\"%s\"\n",
                     aSettingsFile.c_str(), aName.c_str(), aArchitecture, aOsTypeId.c_str(), aFlags.c_str()));

#if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(VBOX_WITH_VIRT_ARMV8)
    if (aArchitecture != PlatformArchitecture_x86)/* x86 hosts only allows creating x86 VMs for now. */
        return setError(VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED, tr("'Creating VMs for platform architecture %s not supported on %s"),
                        Global::stringifyPlatformArchitecture(aArchitecture),
                        Global::stringifyPlatformArchitecture(PlatformArchitecture_x86));
#endif

    StringsList llGroups;
    HRESULT hrc = i_convertMachineGroups(aGroups, &llGroups);
    if (FAILED(hrc))
        return hrc;

    /** @todo r=bird: Would be good to rewrite this parsing using offset into
     *        aFlags and drop all the C pointers, strchr, misguided RTStrStr and
     *        tedious copying of substrings. */
    Utf8Str strCreateFlags(aFlags); /** @todo r=bird: WTF is the point of this copy? */
    Guid id;
    bool fForceOverwrite = false;
    bool fDirectoryIncludesUUID = false;
    if (!strCreateFlags.isEmpty())
    {
        const char *pcszNext = strCreateFlags.c_str();
        while (*pcszNext != '\0')
        {
            Utf8Str strFlag;
            const char *pcszComma = strchr(pcszNext, ','); /*clueless version: RTStrStr(pcszNext, ","); */
            if (!pcszComma)
                strFlag = pcszNext;
            else
                strFlag.assign(pcszNext, (size_t)(pcszComma - pcszNext));

            const char *pcszEqual = strchr(strFlag.c_str(), '='); /* more cluelessness: RTStrStr(strFlag.c_str(), "="); */
            /* skip over everything which doesn't contain '=' */
            if (pcszEqual && pcszEqual != strFlag.c_str())
            {
                Utf8Str strKey(strFlag.c_str(), (size_t)(pcszEqual - strFlag.c_str()));
                Utf8Str strValue(strFlag.c_str() + (pcszEqual - strFlag.c_str() + 1));

                if (strKey == "UUID")
                    id = strValue.c_str();
                else if (strKey == "forceOverwrite")
                    fForceOverwrite = (strValue == "1");
                else if (strKey == "directoryIncludesUUID")
                    fDirectoryIncludesUUID = (strValue == "1");
            }

            if (!pcszComma)
                pcszNext += strFlag.length();  /* you can just 'break' out here... */
            else
                pcszNext += strFlag.length() + 1;
        }
    }

    /* Create UUID if none was specified. */
    if (id.isZero())
        id.create();
    else if (!id.isValid())
    {
        /* do something else */
        return setError(E_INVALIDARG, tr("'%s' is not a valid Guid"), id.toStringCurly().c_str());
    }

    /* NULL settings file means compose automatically */
    Utf8Str strSettingsFile(aSettingsFile);
    if (strSettingsFile.isEmpty())
    {
        Utf8Str strNewCreateFlags(Utf8StrFmt("UUID=%RTuuid", id.raw()));
        if (fDirectoryIncludesUUID)
            strNewCreateFlags += ",directoryIncludesUUID=1";

        com::Utf8Str blstr;
        hrc = composeMachineFilename(aName,
                                     llGroups.front(),
                                     strNewCreateFlags,
                                     blstr /* aBaseFolder */,
                                     strSettingsFile);
        if (FAILED(hrc)) return hrc;
    }

    /* create a new object */
    ComObjPtr<Machine> machine;
    hrc = machine.createObject();
    if (FAILED(hrc)) return hrc;

    ComObjPtr<GuestOSType> osType;
    if (!aOsTypeId.isEmpty())
        i_findGuestOSType(aOsTypeId, osType);

    /* initialize the machine object */
    hrc = machine->init(this,
                        strSettingsFile,
                        aName,
                        aArchitecture,
                        llGroups,
                        aOsTypeId,
                        osType,
                        id,
                        fForceOverwrite,
                        fDirectoryIncludesUUID,
                        aCipher,
                        aPasswordId,
                        aPassword);
    if (SUCCEEDED(hrc))
    {
        /* set the return value */
        machine.queryInterfaceTo(aMachine.asOutParam());
        AssertComRC(hrc);

#ifdef VBOX_WITH_EXTPACK
        /* call the extension pack hooks */
        m->ptrExtPackManager->i_callAllVmCreatedHooks(machine);
#endif
    }

    LogFlowThisFuncLeave();

    return hrc;
}

HRESULT VirtualBox::openMachine(const com::Utf8Str &aSettingsFile,
                                const com::Utf8Str &aPassword,
                                ComPtr<IMachine> &aMachine)
{
    /* create a new object */
    ComObjPtr<Machine> machine;
    HRESULT hrc = machine.createObject();
    if (SUCCEEDED(hrc))
    {
        /* initialize the machine object */
        hrc = machine->initFromSettings(this, aSettingsFile, NULL /* const Guid *aId */, aPassword);
        if (SUCCEEDED(hrc))
        {
            /* set the return value */
            machine.queryInterfaceTo(aMachine.asOutParam());
            ComAssertComRC(hrc);
        }
    }

    return hrc;
}

/** @note Locks objects! */
HRESULT VirtualBox::registerMachine(const ComPtr<IMachine> &aMachine)
{
    Bstr name;
    HRESULT hrc = aMachine->COMGETTER(Name)(name.asOutParam());
    if (FAILED(hrc)) return hrc;

    /* We can safely cast child to Machine * here because only Machine
     * implementations of IMachine can be among our children. */
    IMachine *aM = aMachine;
    Machine *pMachine = static_cast<Machine*>(aM);

    AutoCaller machCaller(pMachine);
    ComAssertComRCRetRC(machCaller.hrc());

    hrc = i_registerMachine(pMachine);
    /* fire an event */
    if (SUCCEEDED(hrc))
        i_onMachineRegistered(pMachine->i_getId(), TRUE);

    return hrc;
}

/** @note Locks this object for reading, then some machine objects for reading. */
HRESULT VirtualBox::findMachine(const com::Utf8Str &aSettingsFile,
                                ComPtr<IMachine> &aMachine)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aSettingsFile=\"%s\", aMachine={%p}\n", aSettingsFile.c_str(), &aMachine));

    /* start with not found */
    HRESULT hrc = S_OK;
    ComObjPtr<Machine> pMachineFound;

    Guid id(aSettingsFile);
    Utf8Str strFile(aSettingsFile);
    if (id.isValid() && !id.isZero())
        hrc = i_findMachine(id,
                            true /* fPermitInaccessible */,
                            true /* setError */,
                            &pMachineFound);
        // returns VBOX_E_OBJECT_NOT_FOUND if not found and sets error
    else
    {
        hrc = i_findMachineByName(strFile,
                                  true /* setError */,
                                  &pMachineFound);
        // returns VBOX_E_OBJECT_NOT_FOUND if not found and sets error
    }

    /* this will set (*machine) to NULL if machineObj is null */
    pMachineFound.queryInterfaceTo(aMachine.asOutParam());

    LogFlowThisFunc(("aName=\"%s\", aMachine=%p, hrc=%08X\n", aSettingsFile.c_str(), &aMachine, hrc));
    LogFlowThisFuncLeave();

    return hrc;
}

HRESULT VirtualBox::getMachinesByGroups(const std::vector<com::Utf8Str> &aGroups,
                                        std::vector<ComPtr<IMachine> > &aMachines)
{
    StringsList llGroups;
    HRESULT hrc = i_convertMachineGroups(aGroups, &llGroups);
    if (FAILED(hrc))
        return hrc;

    /* we want to rely on sorted groups during compare, to save time */
    llGroups.sort();

    /* get copy of all machine references, to avoid holding the list lock */
    MachinesOList::MyList allMachines;
    AutoReadLock al(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    allMachines = m->allMachines.getList();

    std::vector<ComObjPtr<IMachine> > saMachines;
    saMachines.resize(0);
    for (MachinesOList::MyList::const_iterator it = allMachines.begin();
         it != allMachines.end();
         ++it)
    {
        const ComObjPtr<Machine> &pMachine = *it;
        AutoCaller autoMachineCaller(pMachine);
        if (FAILED(autoMachineCaller.hrc()))
            continue;
        AutoReadLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);

        if (pMachine->i_isAccessible())
        {
            const StringsList &thisGroups = pMachine->i_getGroups();
            for (StringsList::const_iterator it2 = thisGroups.begin();
                 it2 != thisGroups.end();
                 ++it2)
            {
                const Utf8Str &group = *it2;
                bool fAppended = false;
                for (StringsList::const_iterator it3 = llGroups.begin();
                     it3 != llGroups.end();
                     ++it3)
                {
                    int order = it3->compare(group);
                    if (order == 0)
                    {
                        saMachines.push_back(static_cast<IMachine *>(pMachine));
                        fAppended = true;
                        break;
                    }
                    else if (order > 0)
                        break;
                    else
                        continue;
                }
                /* avoid duplicates and save time */
                if (fAppended)
                    break;
            }
        }
    }
    aMachines.resize(saMachines.size());
    size_t i = 0;
    for(i = 0; i < saMachines.size(); ++i)
        saMachines[i].queryInterfaceTo(aMachines[i].asOutParam());

    return S_OK;
}

HRESULT VirtualBox::getMachineStates(const std::vector<ComPtr<IMachine> > &aMachines,
                                     std::vector<MachineState_T> &aStates)
{
    com::SafeIfaceArray<IMachine> saMachines(aMachines);
    aStates.resize(aMachines.size());
    for (size_t i = 0; i < saMachines.size(); i++)
    {
        ComPtr<IMachine> pMachine = saMachines[i];
        MachineState_T state = MachineState_Null;
        if (!pMachine.isNull())
        {
            HRESULT hrc = pMachine->COMGETTER(State)(&state);
            if (hrc == E_ACCESSDENIED)
                hrc = S_OK;
            AssertComRC(hrc);
        }
        aStates[i] = state;
    }
    return S_OK;
}

HRESULT VirtualBox::createUnattendedInstaller(ComPtr<IUnattended> &aUnattended)
{
#ifdef VBOX_WITH_UNATTENDED
    ComObjPtr<Unattended> ptrUnattended;
    HRESULT hrc = ptrUnattended.createObject();
    if (SUCCEEDED(hrc))
    {
        AutoReadLock wlock(this COMMA_LOCKVAL_SRC_POS);
        hrc = ptrUnattended->initUnattended(this);
        if (SUCCEEDED(hrc))
            hrc = ptrUnattended.queryInterfaceTo(aUnattended.asOutParam());
    }
    return hrc;
#else
    NOREF(aUnattended);
    return E_NOTIMPL;
#endif
}

HRESULT VirtualBox::createMedium(const com::Utf8Str &aFormat,
                                 const com::Utf8Str &aLocation,
                                 AccessMode_T aAccessMode,
                                 DeviceType_T aDeviceType,
                                 ComPtr<IMedium> &aMedium)
{
    NOREF(aAccessMode); /**< @todo r=klaus make use of access mode */

    HRESULT hrc = S_OK;

    ComObjPtr<Medium> medium;
    medium.createObject();
    com::Utf8Str format = aFormat;

    switch (aDeviceType)
    {
        case DeviceType_HardDisk:
        {

            /* we don't access non-const data members so no need to lock */
            if (format.isEmpty())
                i_getDefaultHardDiskFormat(format);

            hrc = medium->init(this,
                               format,
                               aLocation,
                               Guid::Empty /* media registry: none yet */,
                               aDeviceType);
        }
        break;

        case DeviceType_DVD:
        case DeviceType_Floppy:
        {

            if (format.isEmpty())
                return setError(E_INVALIDARG, tr("Format must be Valid Type%s"), format.c_str());

#if 0 /* unused */
            // enforce read-only for DVDs even if caller specified ReadWrite
            if (aDeviceType == DeviceType_DVD)
                aAccessMode = AccessMode_ReadOnly;
#endif

             hrc = medium->init(this,
                                format,
                                aLocation,
                                Guid::Empty /* media registry: none yet */,
                                aDeviceType);

         }
         break;

         default:
             return setError(E_INVALIDARG, tr("Device type must be HardDisk, DVD or Floppy %d"), aDeviceType);
    }

    if (SUCCEEDED(hrc))
    {
        medium.queryInterfaceTo(aMedium.asOutParam());
        com::Guid uMediumId = medium->i_getId();
        if (uMediumId.isValid() && !uMediumId.isZero())
            i_onMediumRegistered(uMediumId, medium->i_getDeviceType(), TRUE);
    }

    return hrc;
}

HRESULT VirtualBox::openMedium(const com::Utf8Str &aLocation,
                               DeviceType_T aDeviceType,
                               AccessMode_T aAccessMode,
                               BOOL aForceNewUuid,
                               ComPtr<IMedium> &aMedium)
{
    HRESULT hrc = S_OK;
    Guid id(aLocation);
    ComObjPtr<Medium> pMedium;

    // have to get write lock as the whole find/update sequence must be done
    // in one critical section, otherwise there are races which can lead to
    // multiple Medium objects with the same content
    AutoWriteLock treeLock(i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    // check if the device type is correct, and see if a medium for the
    // given path has already initialized; if so, return that
    switch (aDeviceType)
    {
        case DeviceType_HardDisk:
            if (id.isValid() && !id.isZero())
                hrc = i_findHardDiskById(id, false /* setError */, &pMedium);
            else
                hrc = i_findHardDiskByLocation(aLocation, false, /* aSetError */ &pMedium);
        break;

        case DeviceType_Floppy:
        case DeviceType_DVD:
            if (id.isValid() && !id.isZero())
                hrc = i_findDVDOrFloppyImage(aDeviceType, &id, Utf8Str::Empty, false /* setError */, &pMedium);
            else
                hrc = i_findDVDOrFloppyImage(aDeviceType, NULL, aLocation, false /* setError */, &pMedium);

            // enforce read-only for DVDs even if caller specified ReadWrite
            if (aDeviceType == DeviceType_DVD)
                aAccessMode = AccessMode_ReadOnly;
        break;

        default:
            return setError(E_INVALIDARG, tr("Device type must be HardDisk, DVD or Floppy %d"), aDeviceType);
    }

    bool fMediumRegistered = false;
    if (pMedium.isNull())
    {
        pMedium.createObject();
        treeLock.release();
        hrc = pMedium->init(this,
                            aLocation,
                            (aAccessMode == AccessMode_ReadWrite) ? Medium::OpenReadWrite : Medium::OpenReadOnly,
                            !!aForceNewUuid,
                            aDeviceType);
        treeLock.acquire();

        if (SUCCEEDED(hrc))
        {
            hrc = i_registerMedium(pMedium, &pMedium, treeLock);

            treeLock.release();

            /* Note that it's important to call uninit() on failure to register
             * because the differencing hard disk would have been already associated
             * with the parent and this association needs to be broken. */

            if (FAILED(hrc))
            {
                pMedium->uninit();
                hrc = VBOX_E_OBJECT_NOT_FOUND;
            }
            else
                fMediumRegistered = true;
        }
        else if (hrc != VBOX_E_INVALID_OBJECT_STATE)
            hrc = VBOX_E_OBJECT_NOT_FOUND;
    }

    if (SUCCEEDED(hrc))
    {
        pMedium.queryInterfaceTo(aMedium.asOutParam());
        if (fMediumRegistered)
            i_onMediumRegistered(pMedium->i_getId(), pMedium->i_getDeviceType() ,TRUE);
    }

    return hrc;
}


/** @note Locks this object for reading. */
HRESULT VirtualBox::getGuestOSType(const com::Utf8Str &aId,
                                   ComPtr<IGuestOSType> &aType)
{
    ComObjPtr<GuestOSType> pType;
    HRESULT hrc = i_findGuestOSType(aId, pType);
    pType.queryInterfaceTo(aType.asOutParam());
    return hrc;
}

HRESULT VirtualBox::createSharedFolder(const com::Utf8Str &aName,
                                       const com::Utf8Str &aHostPath,
                                       BOOL aWritable,
                                       BOOL aAutomount,
                                       const com::Utf8Str &aAutoMountPoint)
{
    LogFlowThisFunc(("Entering for '%s' -> '%s'\n", aName.c_str(), aHostPath.c_str()));

    ComPtr<ISharedFolder> found;
    HRESULT hrc = i_findSharedFolder(aName, found);
    if (SUCCEEDED(hrc))
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Shared folder named '%s' already exists"),
                        aName.c_str());

    ComObjPtr<SharedFolder> sharedFolder;
    SymlinkPolicy_T enmSymlinkPolicy = SymlinkPolicy_None;
    sharedFolder.createObject();
    hrc = sharedFolder->init(this,
                             aName,
                             aHostPath,
                             !!aWritable,
                             !!aAutomount,
                             aAutoMountPoint,
                             true /* fFailOnError */,
                             enmSymlinkPolicy);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock alock(m->allSharedFolders.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    m->allSharedFolders.addChild(sharedFolder);
    alock.release();
    {
        AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
        vboxLock.release();

        if (FAILED(hrc))
        {
            alock.acquire();
            m->allSharedFolders.removeChild(sharedFolder);
            alock.release();
        }
    }

    i_onSharedFolderChanged();
    LogFlowThisFunc(("Leaving for '%s' -> '%s'\n", aName.c_str(), aHostPath.c_str()));
    return hrc;
}

HRESULT VirtualBox::removeSharedFolder(const com::Utf8Str &aName)
{
    LogFlowThisFunc(("Entering for '%s'\n", aName.c_str()));

    ComPtr<ISharedFolder> sharedFolder;
    HRESULT hrc = i_findSharedFolder(aName, sharedFolder);
    if (FAILED(hrc))
        return hrc;

    ISharedFolder *aP = sharedFolder;
    SharedFolder *aP2 = static_cast<SharedFolder *>(aP);
    AutoWriteLock alock(m->allSharedFolders.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    m->allSharedFolders.removeChild(aP2);
    alock.release();

    {
        AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
        vboxLock.release();

        if (FAILED(hrc))
        {
            alock.acquire();
            m->allSharedFolders.addChild(aP2);
            alock.release();
        }
    }

    i_onSharedFolderChanged();
    LogFlowThisFunc(("Leaving for '%s'\n", aName.c_str()));

    return hrc;
}

/**
 *  @note Locks this object for reading.
 */
HRESULT VirtualBox::getExtraDataKeys(std::vector<com::Utf8Str> &aKeys)
{
    using namespace settings;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aKeys.resize(m->pMainConfigFile->mapExtraDataItems.size());
    size_t i = 0;
    for (StringsMap::const_iterator it = m->pMainConfigFile->mapExtraDataItems.begin();
         it != m->pMainConfigFile->mapExtraDataItems.end(); ++it, ++i)
        aKeys[i] = it->first;

    return S_OK;
}

/**
 *  @note Locks this object for reading.
 */
HRESULT VirtualBox::getExtraData(const com::Utf8Str &aKey,
                                 com::Utf8Str &aValue)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    settings::StringsMap::const_iterator it = m->pMainConfigFile->mapExtraDataItems.find(aKey);
    if (it != m->pMainConfigFile->mapExtraDataItems.end())
        // found:
        aValue = it->second; // source is a Utf8Str

    /* return the result to caller (may be empty) */

    /** @todo r=andy Shouldn't we return an error here if not found? */
    return S_OK;
}

/**
 *  @note Locks this object for writing.
 */
HRESULT VirtualBox::setExtraData(const com::Utf8Str &aKey,
                                 const com::Utf8Str &aValue)
{
    Utf8Str strKey(aKey);
    Utf8Str strValue(aValue);
    Utf8Str strOldValue;            // empty
    HRESULT hrc = S_OK;

    /* Because control characters in aKey have caused problems in the settings
     * they are rejected unless the key should be deleted. */
    if (!strValue.isEmpty())
    {
        for (size_t i = 0; i < strKey.length(); ++i)
        {
            char ch = strKey[i];
            if (RTLocCIsCntrl(ch))
                return E_INVALIDARG;
        }
    }

    // locking note: we only hold the read lock briefly to look up the old value,
    // then release it and call the onExtraCanChange callbacks. There is a small
    // chance of a race insofar as the callback might be called twice if two callers
    // change the same key at the same time, but that's a much better solution
    // than the deadlock we had here before. The actual changing of the extradata
    // is then performed under the write lock and race-free.

    // look up the old value first; if nothing has changed then we need not do anything
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS); // hold read lock only while looking up
        settings::StringsMap::const_iterator it = m->pMainConfigFile->mapExtraDataItems.find(strKey);
        if (it != m->pMainConfigFile->mapExtraDataItems.end())
            strOldValue = it->second;
    }

    bool fChanged;
    if ((fChanged = (strOldValue != strValue)))
    {
        // ask for permission from all listeners outside the locks;
        // onExtraDataCanChange() only briefly requests the VirtualBox
        // lock to copy the list of callbacks to invoke
        Bstr error;

        if (!i_onExtraDataCanChange(Guid::Empty, Bstr(aKey).raw(), Bstr(aValue).raw(), error))
        {
            const char *sep = error.isEmpty() ? "" : ": ";
            Log1WarningFunc(("Someone vetoed! Change refused%s%ls\n", sep, error.raw()));
            return setError(E_ACCESSDENIED,
                            tr("Could not set extra data because someone refused the requested change of '%s' to '%s'%s%ls"),
                            strKey.c_str(),
                            strValue.c_str(),
                            sep,
                            error.raw());
        }

        // data is changing and change not vetoed: then write it out under the lock

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        if (strValue.isEmpty())
            m->pMainConfigFile->mapExtraDataItems.erase(strKey);
        else
            m->pMainConfigFile->mapExtraDataItems[strKey] = strValue;
                // creates a new key if needed

        /* save settings on success */
        hrc = i_saveSettings();
        if (FAILED(hrc)) return hrc;
    }

    // fire notification outside the lock
    if (fChanged)
        i_onExtraDataChanged(Guid::Empty, Bstr(aKey).raw(), Bstr(aValue).raw());

    return hrc;
}

/**
 *
 */
HRESULT VirtualBox::setSettingsSecret(const com::Utf8Str &aPassword)
{
    i_storeSettingsKey(aPassword);
    i_decryptSettings();
    return S_OK;
}

int VirtualBox::i_decryptMediumSettings(Medium *pMedium)
{
    Bstr bstrCipher;
    HRESULT hrc = pMedium->GetProperty(Bstr("InitiatorSecretEncrypted").raw(),
                                       bstrCipher.asOutParam());
    if (SUCCEEDED(hrc))
    {
        Utf8Str strPlaintext;
        int vrc = i_decryptSetting(&strPlaintext, bstrCipher);
        if (RT_SUCCESS(vrc))
            pMedium->i_setPropertyDirect("InitiatorSecret", strPlaintext);
        else
            return vrc;
    }
    return VINF_SUCCESS;
}

/**
 * Decrypt all encrypted settings.
 *
 * So far we only have encrypted iSCSI initiator secrets so we just go through
 * all hard disk media and determine the plain 'InitiatorSecret' from
 * 'InitiatorSecretEncrypted. The latter is stored as Base64 because medium
 * properties need to be null-terminated strings.
 */
int VirtualBox::i_decryptSettings()
{
    bool fFailure = false;
    AutoReadLock al(m->allHardDisks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    for (MediaList::const_iterator mt = m->allHardDisks.begin();
         mt != m->allHardDisks.end();
         ++mt)
    {
        ComObjPtr<Medium> pMedium = *mt;
        AutoCaller medCaller(pMedium);
        if (FAILED(medCaller.hrc()))
            continue;
        AutoWriteLock mlock(pMedium COMMA_LOCKVAL_SRC_POS);
        int vrc = i_decryptMediumSettings(pMedium);
        if (RT_FAILURE(vrc))
            fFailure = true;
    }
    if (!fFailure)
    {
        for (MediaList::const_iterator mt = m->allHardDisks.begin();
             mt != m->allHardDisks.end();
             ++mt)
        {
            i_onMediumConfigChanged(*mt);
        }
    }
    return fFailure ? VERR_INVALID_PARAMETER : VINF_SUCCESS;
}

/**
 * Encode.
 *
 * @param aPlaintext      plaintext to be encrypted
 * @param aCiphertext     resulting ciphertext (base64-encoded)
 */
int VirtualBox::i_encryptSetting(const Utf8Str &aPlaintext, Utf8Str *aCiphertext)
{
    uint8_t abCiphertext[32];
    char    szCipherBase64[128];
    size_t  cchCipherBase64;
    int vrc = i_encryptSettingBytes((uint8_t*)aPlaintext.c_str(), abCiphertext, aPlaintext.length()+1, sizeof(abCiphertext));
    if (RT_SUCCESS(vrc))
    {
        vrc = RTBase64Encode(abCiphertext, sizeof(abCiphertext), szCipherBase64, sizeof(szCipherBase64), &cchCipherBase64);
        if (RT_SUCCESS(vrc))
            *aCiphertext = szCipherBase64;
    }
    return vrc;
}

/**
 * Decode.
 *
 * @param aPlaintext      resulting plaintext
 * @param aCiphertext     ciphertext (base64-encoded) to decrypt
 */
int VirtualBox::i_decryptSetting(Utf8Str *aPlaintext, const Utf8Str &aCiphertext)
{
    uint8_t abPlaintext[64];
    uint8_t abCiphertext[64];
    size_t  cbCiphertext;
    int vrc = RTBase64Decode(aCiphertext.c_str(),
                             abCiphertext, sizeof(abCiphertext),
                             &cbCiphertext, NULL);
    if (RT_SUCCESS(vrc))
    {
        vrc = i_decryptSettingBytes(abPlaintext, abCiphertext, cbCiphertext);
        if (RT_SUCCESS(vrc))
        {
            for (unsigned i = 0; i < cbCiphertext; i++)
            {
                /* sanity check: null-terminated string? */
                if (abPlaintext[i] == '\0')
                {
                    /* sanity check: valid UTF8 string? */
                    if (RTStrIsValidEncoding((const char*)abPlaintext))
                    {
                        *aPlaintext = Utf8Str((const char*)abPlaintext);
                        return VINF_SUCCESS;
                    }
                }
            }
            vrc = VERR_INVALID_MAGIC;
        }
    }
    return vrc;
}

/**
 * Encrypt secret bytes. Use the m->SettingsCipherKey as key.
 *
 * @param aPlaintext      clear text to be encrypted
 * @param aCiphertext     resulting encrypted text
 * @param aPlaintextSize  size of the plaintext
 * @param aCiphertextSize size of the ciphertext
 */
int VirtualBox::i_encryptSettingBytes(const uint8_t *aPlaintext, uint8_t *aCiphertext,
                                    size_t aPlaintextSize, size_t aCiphertextSize) const
{
    unsigned i, j;
    uint8_t aBytes[64];

    if (!m->fSettingsCipherKeySet)
        return VERR_INVALID_STATE;

    if (aCiphertextSize > sizeof(aBytes))
        return VERR_BUFFER_OVERFLOW;

    if (aCiphertextSize < 32)
        return VERR_INVALID_PARAMETER;

    AssertCompile(sizeof(m->SettingsCipherKey) >= 32);

    /* store the first 8 bytes of the cipherkey for verification */
    for (i = 0, j = 0; i < 8; i++, j++)
        aCiphertext[i] = m->SettingsCipherKey[j];

    for (unsigned k = 0; k < aPlaintextSize && i < aCiphertextSize; i++, k++)
    {
        aCiphertext[i] = (aPlaintext[k] ^ m->SettingsCipherKey[j]);
        if (++j >= sizeof(m->SettingsCipherKey))
            j = 0;
    }

    /* fill with random data to have a minimal length (salt) */
    if (i < aCiphertextSize)
    {
        RTRandBytes(aBytes, aCiphertextSize - i);
        for (int k = 0; i < aCiphertextSize; i++, k++)
        {
            aCiphertext[i] = aBytes[k] ^ m->SettingsCipherKey[j];
            if (++j >= sizeof(m->SettingsCipherKey))
                j = 0;
        }
    }

    return VINF_SUCCESS;
}

/**
 * Decrypt secret bytes. Use the m->SettingsCipherKey as key.
 *
 * @param aPlaintext      resulting plaintext
 * @param aCiphertext     ciphertext to be decrypted
 * @param aCiphertextSize size of the ciphertext == size of the plaintext
 */
int VirtualBox::i_decryptSettingBytes(uint8_t *aPlaintext,
                                      const uint8_t *aCiphertext, size_t aCiphertextSize) const
{
    unsigned i, j;

    if (!m->fSettingsCipherKeySet)
        return VERR_INVALID_STATE;

    if (aCiphertextSize < 32)
        return VERR_INVALID_PARAMETER;

    /* key verification */
    for (i = 0, j = 0; i < 8; i++, j++)
        if (aCiphertext[i] != m->SettingsCipherKey[j])
            return VERR_INVALID_MAGIC;

    /* poison */
    memset(aPlaintext, 0xff, aCiphertextSize);
    for (int k = 0; i < aCiphertextSize; i++, k++)
    {
        aPlaintext[k] = aCiphertext[i] ^ m->SettingsCipherKey[j];
        if (++j >= sizeof(m->SettingsCipherKey))
            j = 0;
    }

    return VINF_SUCCESS;
}

/**
 * Store a settings key.
 *
 * @param aKey          the key to store
 */
void VirtualBox::i_storeSettingsKey(const Utf8Str &aKey)
{
    RTSha512(aKey.c_str(), aKey.length(), m->SettingsCipherKey);
    m->fSettingsCipherKeySet = true;
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

#ifdef DEBUG
void VirtualBox::i_dumpAllBackRefs()
{
    {
        AutoReadLock al(m->allHardDisks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
        for (MediaList::const_iterator mt = m->allHardDisks.begin();
             mt != m->allHardDisks.end();
             ++mt)
        {
            ComObjPtr<Medium> pMedium = *mt;
            pMedium->i_dumpBackRefs();
        }
    }
    {
        AutoReadLock al(m->allDVDImages.getLockHandle() COMMA_LOCKVAL_SRC_POS);
        for (MediaList::const_iterator mt = m->allDVDImages.begin();
             mt != m->allDVDImages.end();
             ++mt)
        {
            ComObjPtr<Medium> pMedium = *mt;
            pMedium->i_dumpBackRefs();
        }
    }
}
#endif

/**
 *  Posts an event to the event queue that is processed asynchronously
 *  on a dedicated thread.
 *
 *  Posting events to the dedicated event queue is useful to perform secondary
 *  actions outside any object locks -- for example, to iterate over a list
 *  of callbacks and inform them about some change caused by some object's
 *  method call.
 *
 *  @param event    event to post; must have been allocated using |new|, will
 *                  be deleted automatically by the event thread after processing
 *
 *  @note Doesn't lock any object.
 */
HRESULT VirtualBox::i_postEvent(Event *event)
{
    AssertReturn(event, E_FAIL);

    HRESULT hrc;
    AutoCaller autoCaller(this);
    if (SUCCEEDED((hrc = autoCaller.hrc())))
    {
        if (getObjectState().getState() != ObjectState::Ready)
            Log1WarningFunc(("VirtualBox has been uninitialized (state=%d), the event is discarded!\n",
                             getObjectState().getState()));
            // return S_OK
        else if (    (m->pAsyncEventQ)
                  && (m->pAsyncEventQ->postEvent(event))
                )
            return S_OK;
        else
            hrc = E_FAIL;
    }

    // in any event of failure, we must clean up here, or we'll leak;
    // the caller has allocated the object using new()
    delete event;
    return hrc;
}

/**
 * Adds a progress to the global collection of pending operations.
 * Usually gets called upon progress object initialization.
 *
 * @param aProgress Operation to add to the collection.
 *
 * @note Doesn't lock objects.
 */
HRESULT VirtualBox::i_addProgress(IProgress *aProgress)
{
    CheckComArgNotNull(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    Bstr id;
    HRESULT hrc = aProgress->COMGETTER(Id)(id.asOutParam());
    AssertComRCReturnRC(hrc);

    /* protect mProgressOperations */
    AutoWriteLock safeLock(m->mtxProgressOperations COMMA_LOCKVAL_SRC_POS);

    m->mapProgressOperations.insert(ProgressMap::value_type(Guid(id), aProgress));
    return S_OK;
}

/**
 * Removes the progress from the global collection of pending operations.
 * Usually gets called upon progress completion.
 *
 * @param aId   UUID of the progress operation to remove
 *
 * @note Doesn't lock objects.
 */
HRESULT VirtualBox::i_removeProgress(IN_GUID aId)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    ComPtr<IProgress> progress;

    /* protect mProgressOperations */
    AutoWriteLock safeLock(m->mtxProgressOperations COMMA_LOCKVAL_SRC_POS);

    size_t cnt = m->mapProgressOperations.erase(aId);
    Assert(cnt == 1);
    NOREF(cnt);

    return S_OK;
}

#ifdef RT_OS_WINDOWS

class StartSVCHelperClientData : public ThreadTask
{
public:
    StartSVCHelperClientData()
    {
        LogFlowFuncEnter();
        m_strTaskName = "SVCHelper";
        threadVoidData = NULL;
        initialized = false;
        privileged = false;
        func = NULL;
        user = NULL;
    }

    virtual ~StartSVCHelperClientData()
    {
        LogFlowFuncEnter();
        if (threadVoidData!=NULL)
        {
            delete threadVoidData;
            threadVoidData=NULL;
        }
    };

    void handler()
    {
        VirtualBox::i_SVCHelperClientThreadTask(this);
    }

    const ComPtr<Progress>& GetProgressObject() const {return progress;}

    bool init(VirtualBox* aVbox,
              Progress* aProgress,
              bool aPrivileged,
              VirtualBox::PFN_SVC_HELPER_CLIENT_T aFunc,
              void *aUser)
    {
        LogFlowFuncEnter();
        that = aVbox;
        progress = aProgress;
        privileged = aPrivileged;
        func = aFunc;
        user = aUser;

        initThreadVoidData();

        initialized = true;

        return initialized;
    }

    bool isOk() const{ return initialized;}

    bool initialized;
    ComObjPtr<VirtualBox> that;
    ComObjPtr<Progress> progress;
    bool privileged;
    VirtualBox::PFN_SVC_HELPER_CLIENT_T func;
    void *user;
    ThreadVoidData *threadVoidData;

private:
    bool initThreadVoidData()
    {
        LogFlowFuncEnter();
        threadVoidData = static_cast<ThreadVoidData*>(user);
        return true;
    }
};

/**
 *  Helper method that starts a worker thread that:
 *  - creates a pipe communication channel using SVCHlpClient;
 *  - starts an SVC Helper process that will inherit this channel;
 *  - executes the supplied function by passing it the created SVCHlpClient
 *    and opened instance to communicate to the Helper process and the given
 *    Progress object.
 *
 *  The user function is supposed to communicate to the helper process
 *  using the \a aClient argument to do the requested job and optionally expose
 *  the progress through the \a aProgress object. The user function should never
 *  call notifyComplete() on it: this will be done automatically using the
 *  result code returned by the function.
 *
 *  Before the user function is started, the communication channel passed to
 *  the \a aClient argument is fully set up, the function should start using
 *  its write() and read() methods directly.
 *
 *  The \a aVrc parameter of the user function may be used to return an error
 *  code if it is related to communication errors (for example, returned by
 *  the SVCHlpClient members when they fail). In this case, the correct error
 *  message using this value will be reported to the caller. Note that the
 *  value of \a aVrc is inspected only if the user function itself returns
 *  success.
 *
 *  If a failure happens anywhere before the user function would be normally
 *  called, it will be called anyway in special "cleanup only" mode indicated
 *  by \a aClient, \a aProgress and \a aVrc arguments set to NULL. In this mode,
 *  all the function is supposed to do is to cleanup its aUser argument if
 *  necessary (it's assumed that the ownership of this argument is passed to
 *  the user function once #startSVCHelperClient() returns a success, thus
 *  making it responsible for the cleanup).
 *
 *  After the user function returns, the thread will send the SVCHlpMsg::Null
 *  message to indicate a process termination.
 *
 *  @param  aPrivileged |true| to start the SVC Helper process as a privileged
 *                      user that can perform administrative tasks
 *  @param  aFunc       user function to run
 *  @param  aUser       argument to the user function
 *  @param  aProgress   progress object that will track operation completion
 *
 *  @note aPrivileged is currently ignored (due to some unsolved problems in
 *        Vista) and the process will be started as a normal (unprivileged)
 *        process.
 *
 *  @note Doesn't lock anything.
 */
HRESULT VirtualBox::i_startSVCHelperClient(bool aPrivileged,
                                           PFN_SVC_HELPER_CLIENT_T aFunc,
                                           void *aUser, Progress *aProgress)
{
    LogFlowFuncEnter();
    AssertReturn(aFunc, E_POINTER);
    AssertReturn(aProgress, E_POINTER);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* create the i_SVCHelperClientThreadTask() argument */

    HRESULT hrc = S_OK;
    StartSVCHelperClientData *pTask = NULL;
    try
    {
        pTask = new StartSVCHelperClientData();

        pTask->init(this, aProgress, aPrivileged, aFunc, aUser);

        if (!pTask->isOk())
        {
            delete pTask;
            LogRel(("Could not init StartSVCHelperClientData object \n"));
            throw E_FAIL;
        }

        //this function delete pTask in case of exceptions, so there is no need in the call of delete operator
        hrc = pTask->createThreadWithType(RTTHREADTYPE_MAIN_WORKER);

    }
    catch(std::bad_alloc &)
    {
        hrc = setError(E_OUTOFMEMORY);
    }
    catch(...)
    {
        LogRel(("Could not create thread for StartSVCHelperClientData \n"));
        hrc = E_FAIL;
    }

    return hrc;
}

/**
 *  Worker thread for startSVCHelperClient().
 */
/* static */
void VirtualBox::i_SVCHelperClientThreadTask(StartSVCHelperClientData *pTask)
{
    LogFlowFuncEnter();
    HRESULT hrc = S_OK;
    bool userFuncCalled = false;

    do
    {
        AssertBreakStmt(pTask, hrc = E_POINTER);
        AssertReturnVoid(!pTask->progress.isNull());

        /* protect VirtualBox from uninitialization */
        AutoCaller autoCaller(pTask->that);
        if (!autoCaller.isOk())
        {
            /* it's too late */
            hrc = autoCaller.hrc();
            break;
        }

        int vrc = VINF_SUCCESS;

        Guid id;
        id.create();
        SVCHlpClient client;
        vrc = client.create(Utf8StrFmt("VirtualBox\\SVCHelper\\{%RTuuid}",
                                       id.raw()).c_str());
        if (RT_FAILURE(vrc))
        {
            hrc = pTask->that->setErrorBoth(E_FAIL, vrc, tr("Could not create the communication channel (%Rrc)"), vrc);
            break;
        }

        /* get the path to the executable */
        char exePathBuf[RTPATH_MAX];
        char *exePath = RTProcGetExecutablePath(exePathBuf, RTPATH_MAX);
        if (!exePath)
        {
            hrc = pTask->that->setError(E_FAIL, tr("Cannot get executable name"));
            break;
        }

        Utf8Str argsStr = Utf8StrFmt("/Helper %s", client.name().c_str());

        LogFlowFunc(("Starting '\"%s\" %s'...\n", exePath, argsStr.c_str()));

        RTPROCESS pid = NIL_RTPROCESS;

        if (pTask->privileged)
        {
            /* Attempt to start a privileged process using the Run As dialog */

            Bstr file = exePath;
            Bstr parameters = argsStr;

            SHELLEXECUTEINFO shExecInfo;

            shExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);

            shExecInfo.fMask = NULL;
            shExecInfo.hwnd = NULL;
            shExecInfo.lpVerb = L"runas";
            shExecInfo.lpFile = file.raw();
            shExecInfo.lpParameters = parameters.raw();
            shExecInfo.lpDirectory = NULL;
            shExecInfo.nShow = SW_NORMAL;
            shExecInfo.hInstApp = NULL;

            if (!ShellExecuteEx(&shExecInfo))
            {
                int vrc2 = RTErrConvertFromWin32(GetLastError());
                /* hide excessive details in case of a frequent error
                 * (pressing the Cancel button to close the Run As dialog) */
                if (vrc2 == VERR_CANCELLED)
                    hrc = pTask->that->setErrorBoth(E_FAIL, vrc, tr("Operation canceled by the user"));
                else
                    hrc = pTask->that->setErrorBoth(E_FAIL, vrc, tr("Could not launch a privileged process '%s' (%Rrc)"), exePath, vrc2);
                break;
            }
        }
        else
        {
            const char *args[] = { exePath, "/Helper", client.name().c_str(), 0 };
            vrc = RTProcCreate(exePath, args, RTENV_DEFAULT, 0, &pid);
            if (RT_FAILURE(vrc))
            {
                hrc = pTask->that->setErrorBoth(E_FAIL, vrc, tr("Could not launch a process '%s' (%Rrc)"), exePath, vrc);
                break;
            }
        }

        /* wait for the client to connect */
        vrc = client.connect();
        if (RT_SUCCESS(vrc))
        {
            /* start the user supplied function */
            hrc = pTask->func(&client, pTask->progress, pTask->user, &vrc);
            userFuncCalled = true;
        }

        /* send the termination signal to the process anyway */
        {
            int vrc2 = client.write(SVCHlpMsg::Null);
            if (RT_SUCCESS(vrc))
                vrc = vrc2;
        }

        if (SUCCEEDED(hrc) && RT_FAILURE(vrc))
        {
            hrc = pTask->that->setErrorBoth(E_FAIL, vrc, tr("Could not operate the communication channel (%Rrc)"), vrc);
            break;
        }
    }
    while (0);

    if (FAILED(hrc) && !userFuncCalled)
    {
        /* call the user function in the "cleanup only" mode
         * to let it free resources passed to in aUser */
        pTask->func(NULL, NULL, pTask->user, NULL);
    }

    pTask->progress->i_notifyComplete(hrc);

    LogFlowFuncLeave();
}

#endif /* RT_OS_WINDOWS */

/**
 *  Sends a signal to the client watcher to rescan the set of machines
 *  that have open sessions.
 *
 *  @note Doesn't lock anything.
 */
void VirtualBox::i_updateClientWatcher()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AssertPtrReturnVoid(m->pClientWatcher);
    m->pClientWatcher->update();
}

/**
 *  Adds the given child process ID to the list of processes to be reaped.
 *  This call should be followed by #i_updateClientWatcher() to take the effect.
 *
 *  @note Doesn't lock anything.
 */
void VirtualBox::i_addProcessToReap(RTPROCESS pid)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AssertPtrReturnVoid(m->pClientWatcher);
    m->pClientWatcher->addProcess(pid);
}

/**
 * VD plugin load
 */
int VirtualBox::i_loadVDPlugin(const char *pszPluginLibrary)
{
    return m->pSystemProperties->i_loadVDPlugin(pszPluginLibrary);
}

/**
 * VD plugin unload
 */
int VirtualBox::i_unloadVDPlugin(const char *pszPluginLibrary)
{
    return m->pSystemProperties->i_unloadVDPlugin(pszPluginLibrary);
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onMediumRegistered(const Guid &aMediumId, const DeviceType_T aDevType, const BOOL aRegistered)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateMediumRegisteredEvent(ptrEvent.asOutParam(), m->pEventSource,
                                                aMediumId.toString(), aDevType, aRegistered);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

void VirtualBox::i_onMediumConfigChanged(IMedium *aMedium)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateMediumConfigChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aMedium);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

void VirtualBox::i_onMediumChanged(IMediumAttachment *aMediumAttachment)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateMediumChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aMediumAttachment);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Locks this object for reading.
 */
void VirtualBox::i_onSharedFolderChanged()
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    SessionMachinesList aMachines;
    i_getOpenedMachines(aMachines, NULL);
    for (SessionMachinesList::iterator it = aMachines.begin();
         it != aMachines.end();
         ++it)
    {
        ComObjPtr<SessionMachine> &pMachine = *it;
        AutoReadLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);
        pMachine->i_onSharedFolderChange(TRUE);
    }
    aMachines.clear();
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onStorageControllerChanged(const Guid &aMachineId, const com::Utf8Str &aControllerName)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateStorageControllerChangedEvent(ptrEvent.asOutParam(), m->pEventSource,
                                                        aMachineId.toString(), aControllerName);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

void VirtualBox::i_onStorageDeviceChanged(IMediumAttachment *aStorageDevice, const BOOL fRemoved, const BOOL fSilent)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateStorageDeviceChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aStorageDevice, fRemoved, fSilent);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onMachineStateChanged(const Guid &aId, MachineState_T aState)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateMachineStateChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aId.toString(), aState);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onMachineDataChanged(const Guid &aId, BOOL aTemporary)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateMachineDataChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aId.toString(), aTemporary);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onMachineGroupsChanged(const Guid &aId)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateMachineGroupsChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aId.toString(), FALSE /*aDummy*/);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Locks this object for reading.
 */
BOOL VirtualBox::i_onExtraDataCanChange(const Guid &aId, const Utf8Str &aKey, const Utf8Str &aValue, Bstr &aError)
{
    LogFlowThisFunc(("machine={%RTuuid} aKey={%s} aValue={%s}\n", aId.raw(), aKey.c_str(), aValue.c_str()));

    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), FALSE);

    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateExtraDataCanChangeEvent(ptrEvent.asOutParam(), m->pEventSource, aId.toString(), aKey, aValue);
    AssertComRCReturn(hrc, TRUE);

    VBoxEventDesc EvtDesc(ptrEvent, m->pEventSource);
    BOOL fDelivered = EvtDesc.fire(3000); /* Wait up to 3 secs for delivery */
    //Assert(fDelivered);
    BOOL fAllowChange = TRUE;
    if (fDelivered)
    {
        ComPtr<IExtraDataCanChangeEvent> ptrCanChangeEvent = ptrEvent;
        Assert(ptrCanChangeEvent);

        BOOL fVetoed = FALSE;
        ptrCanChangeEvent->IsVetoed(&fVetoed);
        fAllowChange = !fVetoed;

        if (!fAllowChange)
        {
            SafeArray<BSTR> aVetos;
            ptrCanChangeEvent->GetVetos(ComSafeArrayAsOutParam(aVetos));
            if (aVetos.size() > 0)
                aError = aVetos[0];
        }
    }

    LogFlowThisFunc(("fAllowChange=%RTbool\n", fAllowChange));
    return fAllowChange;
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onExtraDataChanged(const Guid &aId, const Utf8Str &aKey, const Utf8Str &aValue)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateExtraDataChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aId.toString(), aKey, aValue);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onMachineRegistered(const Guid &aId, BOOL aRegistered)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateMachineRegisteredEvent(ptrEvent.asOutParam(), m->pEventSource, aId.toString(), aRegistered);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onSessionStateChanged(const Guid &aId, SessionState_T aState)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateSessionStateChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aId.toString(), aState);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onSnapshotTaken(const Guid &aMachineId, const Guid &aSnapshotId)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateSnapshotTakenEvent(ptrEvent.asOutParam(), m->pEventSource,
                                             aMachineId.toString(), aSnapshotId.toString());
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onSnapshotDeleted(const Guid &aMachineId, const Guid &aSnapshotId)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateSnapshotDeletedEvent(ptrEvent.asOutParam(), m->pEventSource,
                                               aMachineId.toString(), aSnapshotId.toString());
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onSnapshotRestored(const Guid &aMachineId, const Guid &aSnapshotId)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateSnapshotRestoredEvent(ptrEvent.asOutParam(), m->pEventSource,
                                                aMachineId.toString(), aSnapshotId.toString());
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onSnapshotChanged(const Guid &aMachineId, const Guid &aSnapshotId)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateSnapshotChangedEvent(ptrEvent.asOutParam(), m->pEventSource,
                                               aMachineId.toString(), aSnapshotId.toString());
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onGuestPropertyChanged(const Guid &aMachineId, const Utf8Str &aName, const Utf8Str &aValue,
                                          const Utf8Str &aFlags, const BOOL fWasDeleted)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateGuestPropertyChangedEvent(ptrEvent.asOutParam(), m->pEventSource,
                                                    aMachineId.toString(), aName, aValue, aFlags, fWasDeleted);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onNatRedirectChanged(const Guid &aMachineId, ULONG ulSlot, bool fRemove, const Utf8Str &aName,
                                        NATProtocol_T aProto, const Utf8Str &aHostIp, uint16_t aHostPort,
                                        const Utf8Str &aGuestIp, uint16_t aGuestPort)
{
    ::FireNATRedirectEvent(m->pEventSource, aMachineId.toString(), ulSlot, fRemove, aName, aProto, aHostIp,
                           aHostPort, aGuestIp, aGuestPort);
}

/** @todo Unused!!  */
void VirtualBox::i_onNATNetworkChanged(const Utf8Str &aName)
{
    ::FireNATNetworkChangedEvent(m->pEventSource, aName);
}

void VirtualBox::i_onNATNetworkStartStop(const Utf8Str &aName, BOOL fStart)
{
    ::FireNATNetworkStartStopEvent(m->pEventSource, aName, fStart);
}

void VirtualBox::i_onNATNetworkSetting(const Utf8Str &aNetworkName, BOOL aEnabled,
                                       const Utf8Str &aNetwork, const Utf8Str &aGateway,
                                       BOOL aAdvertiseDefaultIpv6RouteEnabled,
                                       BOOL fNeedDhcpServer)
{
    ::FireNATNetworkSettingEvent(m->pEventSource, aNetworkName, aEnabled, aNetwork, aGateway,
                                 aAdvertiseDefaultIpv6RouteEnabled, fNeedDhcpServer);
}

void VirtualBox::i_onNATNetworkPortForward(const Utf8Str &aNetworkName, BOOL create, BOOL fIpv6,
                                           const Utf8Str &aRuleName, NATProtocol_T proto,
                                           const Utf8Str &aHostIp, LONG aHostPort,
                                           const Utf8Str &aGuestIp, LONG aGuestPort)
{
    ::FireNATNetworkPortForwardEvent(m->pEventSource, aNetworkName, create, fIpv6, aRuleName, proto,
                                     aHostIp, aHostPort, aGuestIp, aGuestPort);
}


void VirtualBox::i_onHostNameResolutionConfigurationChange()
{
    if (m->pEventSource)
        ::FireHostNameResolutionConfigurationChangeEvent(m->pEventSource);
}


int VirtualBox::i_natNetworkRefInc(const Utf8Str &aNetworkName)
{
    AutoWriteLock safeLock(*spMtxNatNetworkNameToRefCountLock COMMA_LOCKVAL_SRC_POS);

    if (!sNatNetworkNameToRefCount[aNetworkName])
    {
        ComPtr<INATNetwork> nat;
        HRESULT hrc = findNATNetworkByName(aNetworkName, nat);
        if (FAILED(hrc)) return -1;

        hrc = nat->Start();
        if (SUCCEEDED(hrc))
            LogRel(("Started NAT network '%s'\n", aNetworkName.c_str()));
        else
            LogRel(("Error %Rhrc starting NAT network '%s'\n", hrc, aNetworkName.c_str()));
        AssertComRCReturn(hrc, -1);
    }

    sNatNetworkNameToRefCount[aNetworkName]++;

    return sNatNetworkNameToRefCount[aNetworkName];
}


int VirtualBox::i_natNetworkRefDec(const Utf8Str &aNetworkName)
{
    AutoWriteLock safeLock(*spMtxNatNetworkNameToRefCountLock COMMA_LOCKVAL_SRC_POS);

    if (!sNatNetworkNameToRefCount[aNetworkName])
        return 0;

    sNatNetworkNameToRefCount[aNetworkName]--;

    if (!sNatNetworkNameToRefCount[aNetworkName])
    {
        ComPtr<INATNetwork> nat;
        HRESULT hrc = findNATNetworkByName(aNetworkName, nat);
        if (FAILED(hrc)) return -1;

        hrc = nat->Stop();
        if (SUCCEEDED(hrc))
            LogRel(("Stopped NAT network '%s'\n", aNetworkName.c_str()));
        else
            LogRel(("Error %Rhrc stopping NAT network '%s'\n", hrc, aNetworkName.c_str()));
        AssertComRCReturn(hrc, -1);
    }

    return sNatNetworkNameToRefCount[aNetworkName];
}


/*
 * Export this to NATNetwork so that its setters can refuse to change
 * essential network settings when an VBoxNatNet instance is running.
 */
RWLockHandle *VirtualBox::i_getNatNetLock() const
{
    return spMtxNatNetworkNameToRefCountLock;
}


/*
 * Export this to NATNetwork so that its setters can refuse to change
 * essential network settings when an VBoxNatNet instance is running.
 * The caller is expected to hold a read lock on i_getNatNetLock().
 */
bool VirtualBox::i_isNatNetStarted(const Utf8Str &aNetworkName) const
{
    return sNatNetworkNameToRefCount[aNetworkName] > 0;
}


void VirtualBox::i_onCloudProviderListChanged(BOOL aRegistered)
{
    ::FireCloudProviderListChangedEvent(m->pEventSource, aRegistered);
}


void VirtualBox::i_onCloudProviderRegistered(const Utf8Str &aProviderId, BOOL aRegistered)
{
    ::FireCloudProviderRegisteredEvent(m->pEventSource, aProviderId, aRegistered);
}


void VirtualBox::i_onCloudProviderUninstall(const Utf8Str &aProviderId)
{
    HRESULT hrc;

    ComPtr<IEvent> pEvent;
    hrc = CreateCloudProviderUninstallEvent(pEvent.asOutParam(),
                                            m->pEventSource, aProviderId);
    if (FAILED(hrc))
        return;

    BOOL fDelivered = FALSE;
    hrc = m->pEventSource->FireEvent(pEvent, /* :timeout */ 10000, &fDelivered);
    if (FAILED(hrc))
        return;
}

void VirtualBox::i_onLanguageChanged(const Utf8Str &aLanguageId)
{
    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = ::CreateLanguageChangedEvent(ptrEvent.asOutParam(), m->pEventSource, aLanguageId);
    AssertComRCReturnVoid(hrc);
    i_postEvent(new AsyncEvent(this, ptrEvent));
}

void VirtualBox::i_onProgressCreated(const Guid &aId, BOOL aCreated)
{
    ::FireProgressCreatedEvent(m->pEventSource, aId.toString(), aCreated);
}

#ifdef VBOX_WITH_UPDATE_AGENT
/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onUpdateAgentAvailable(IUpdateAgent *aAgent,
                                          const Utf8Str &aVer, UpdateChannel_T aChannel, UpdateSeverity_T aSev,
                                          const Utf8Str &aDownloadURL, const Utf8Str &aWebURL, const Utf8Str &aReleaseNotes)
{
    ::FireUpdateAgentAvailableEvent(m->pEventSource, aAgent, aVer, aChannel, aSev,
                                    aDownloadURL, aWebURL, aReleaseNotes);
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onUpdateAgentError(IUpdateAgent *aAgent, const Utf8Str &aErrMsg, LONG aRc)
{
    ::FireUpdateAgentErrorEvent(m->pEventSource, aAgent, aErrMsg, aRc);
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onUpdateAgentStateChanged(IUpdateAgent *aAgent, UpdateState_T aState)
{
    ::FireUpdateAgentStateChangedEvent(m->pEventSource, aAgent, aState);
}

/**
 *  @note Doesn't lock any object.
 */
void VirtualBox::i_onUpdateAgentSettingsChanged(IUpdateAgent *aAgent, const Utf8Str &aAttributeHint)
{
    ::FireUpdateAgentSettingsChangedEvent(m->pEventSource, aAgent, aAttributeHint);
}
#endif /* VBOX_WITH_UPDATE_AGENT */

#ifdef VBOX_WITH_EXTPACK
void VirtualBox::i_onExtPackInstalled(const Utf8Str &aExtPackName)
{
    ::FireExtPackInstalledEvent(m->pEventSource, aExtPackName);
}

void VirtualBox::i_onExtPackUninstalled(const Utf8Str &aExtPackName)
{
    ::FireExtPackUninstalledEvent(m->pEventSource, aExtPackName);
}
#endif

/**
 *  @note Locks the list of other objects for reading.
 */
ComObjPtr<GuestOSType> VirtualBox::i_getUnknownOSType()
{
    ComObjPtr<GuestOSType> type;

    /* unknown type must always be the first */
    ComAssertRet(m->allGuestOSTypes.size() > 0, type);

    return m->allGuestOSTypes.front();
}

/**
 * Returns the list of opened machines (machines having VM sessions opened,
 * ignoring other sessions) and optionally the list of direct session controls.
 *
 * @param aMachines     Where to put opened machines (will be empty if none).
 * @param aControls     Where to put direct session controls (optional).
 *
 * @note The returned lists contain smart pointers. So, clear it as soon as
 * it becomes no more necessary to release instances.
 *
 * @note It can be possible that a session machine from the list has been
 * already uninitialized, so do a usual AutoCaller/AutoReadLock sequence
 * when accessing unprotected data directly.
 *
 * @note Locks objects for reading.
 */
void VirtualBox::i_getOpenedMachines(SessionMachinesList &aMachines,
                                     InternalControlList *aControls /*= NULL*/)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    aMachines.clear();
    if (aControls)
        aControls->clear();

    AutoReadLock alock(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (MachinesOList::iterator it = m->allMachines.begin();
         it != m->allMachines.end();
         ++it)
    {
        ComObjPtr<SessionMachine> sm;
        ComPtr<IInternalSessionControl> ctl;
        if ((*it)->i_isSessionOpenVM(sm, &ctl))
        {
            aMachines.push_back(sm);
            if (aControls)
                aControls->push_back(ctl);
        }
    }
}

/**
 * Gets a reference to the machine list. This is the real thing, not a copy,
 * so bad things will happen if the caller doesn't hold the necessary lock.
 *
 * @returns reference to machine list
 *
 * @note Caller must hold the VirtualBox object lock at least for reading.
 */
VirtualBox::MachinesOList &VirtualBox::i_getMachinesList(void)
{
    return m->allMachines;
}

/**
 *  Searches for a machine object with the given ID in the collection
 *  of registered machines.
 *
 * @param aId Machine UUID to look for.
 * @param fPermitInaccessible If true, inaccessible machines will be found;
 *                  if false, this will fail if the given machine is inaccessible.
 * @param aSetError If true, set errorinfo if the machine is not found.
 * @param aMachine Returned machine, if found.
 * @return
 */
HRESULT VirtualBox::i_findMachine(const Guid &aId,
                                  bool fPermitInaccessible,
                                  bool aSetError,
                                  ComObjPtr<Machine> *aMachine /* = NULL */)
{
    HRESULT hrc = VBOX_E_OBJECT_NOT_FOUND;

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    {
        AutoReadLock al(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);

        for (MachinesOList::iterator it = m->allMachines.begin();
             it != m->allMachines.end();
             ++it)
        {
            ComObjPtr<Machine> pMachine = *it;

            if (!fPermitInaccessible)
            {
                // skip inaccessible machines
                AutoCaller machCaller(pMachine);
                if (FAILED(machCaller.hrc()))
                    continue;
            }

            if (pMachine->i_getId() == aId)
            {
                hrc = S_OK;
                if (aMachine)
                    *aMachine = pMachine;
                break;
            }
        }
    }

    if (aSetError && FAILED(hrc))
        hrc = setError(hrc, tr("Could not find a registered machine with UUID {%RTuuid}"), aId.raw());

    return hrc;
}

/**
 * Searches for a machine object with the given name or location in the
 * collection of registered machines.
 *
 * @param aName Machine name or location to look for.
 * @param aSetError If true, set errorinfo if the machine is not found.
 * @param aMachine Returned machine, if found.
 * @return
 */
HRESULT VirtualBox::i_findMachineByName(const Utf8Str &aName,
                                        bool  aSetError,
                                        ComObjPtr<Machine> *aMachine /* = NULL */)
{
    HRESULT hrc = VBOX_E_OBJECT_NOT_FOUND;

    AutoReadLock al(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    for (MachinesOList::iterator it = m->allMachines.begin();
         it != m->allMachines.end();
         ++it)
    {
        ComObjPtr<Machine> &pMachine = *it;
        AutoCaller machCaller(pMachine);
        if (!machCaller.isOk())
            continue;       // we can't ask inaccessible machines for their names

        AutoReadLock machLock(pMachine COMMA_LOCKVAL_SRC_POS);
        if (pMachine->i_getName() == aName)
        {
            hrc = S_OK;
            if (aMachine)
                *aMachine = pMachine;
            break;
        }
        if (!RTPathCompare(pMachine->i_getSettingsFileFull().c_str(), aName.c_str()))
        {
            hrc = S_OK;
            if (aMachine)
                *aMachine = pMachine;
            break;
        }
    }

    if (aSetError && FAILED(hrc))
        hrc = setError(hrc, tr("Could not find a registered machine named '%s'"), aName.c_str());

    return hrc;
}

static HRESULT i_validateMachineGroupHelper(const Utf8Str &aGroup, bool fPrimary, VirtualBox *pVirtualBox)
{
    /* empty strings are invalid */
    if (aGroup.isEmpty())
        return E_INVALIDARG;
    /* the toplevel group is valid */
    if (aGroup == "/")
        return S_OK;
    /* any other strings of length 1 are invalid */
    if (aGroup.length() == 1)
        return E_INVALIDARG;
    /* must start with a slash */
    if (aGroup.c_str()[0] != '/')
        return E_INVALIDARG;
    /* must not end with a slash */
    if (aGroup.c_str()[aGroup.length() - 1] == '/')
        return E_INVALIDARG;
    /* check the group components */
    const char *pStr = aGroup.c_str() + 1;  /* first char is /, skip it */
    while (pStr)
    {
        char *pSlash = RTStrStr(pStr, "/");
        if (pSlash)
        {
            /* no empty components (or // sequences in other words) */
            if (pSlash == pStr)
                return E_INVALIDARG;
            /* check if the machine name rules are violated, because that means
             * the group components are too close to the limits. */
            Utf8Str tmp((const char *)pStr, (size_t)(pSlash - pStr));
            Utf8Str tmp2(tmp);
            sanitiseMachineFilename(tmp);
            if (tmp != tmp2)
                return E_INVALIDARG;
            if (fPrimary)
            {
                HRESULT hrc = pVirtualBox->i_findMachineByName(tmp, false /* aSetError */);
                if (SUCCEEDED(hrc))
                    return VBOX_E_VM_ERROR;
            }
            pStr = pSlash + 1;
        }
        else
        {
            /* check if the machine name rules are violated, because that means
             * the group components is too close to the limits. */
            Utf8Str tmp(pStr);
            Utf8Str tmp2(tmp);
            sanitiseMachineFilename(tmp);
            if (tmp != tmp2)
                return E_INVALIDARG;
            pStr = NULL;
        }
    }
    return S_OK;
}

/**
 * Validates a machine group.
 *
 * @param aGroup    Machine group.
 * @param fPrimary  Set if this is the primary group.
 *
 * @return S_OK or E_INVALIDARG
 */
HRESULT VirtualBox::i_validateMachineGroup(const Utf8Str &aGroup, bool fPrimary)
{
    HRESULT hrc = i_validateMachineGroupHelper(aGroup, fPrimary, this);
    if (FAILED(hrc))
    {
        if (hrc == VBOX_E_VM_ERROR)
            hrc = setError(E_INVALIDARG, tr("Machine group '%s' conflicts with a virtual machine name"), aGroup.c_str());
        else
            hrc = setError(hrc, tr("Invalid machine group '%s'"), aGroup.c_str());
    }
    return hrc;
}

/**
 * Takes a list of machine groups, and sanitizes/validates it.
 *
 * @param aMachineGroups    Array with the machine groups.
 * @param pllMachineGroups  Pointer to list of strings for the result.
 *
 * @return S_OK or E_INVALIDARG
 */
HRESULT VirtualBox::i_convertMachineGroups(const std::vector<com::Utf8Str> aMachineGroups, StringsList *pllMachineGroups)
{
    pllMachineGroups->clear();
    if (aMachineGroups.size())
    {
        for (size_t i = 0; i < aMachineGroups.size(); i++)
        {
            Utf8Str group(aMachineGroups[i]);
            if (group.length() == 0)
                group = "/";

            HRESULT hrc = i_validateMachineGroup(group, i == 0);
            if (FAILED(hrc))
                return hrc;

            /* no duplicates please */
            if (   find(pllMachineGroups->begin(), pllMachineGroups->end(), group)
                == pllMachineGroups->end())
                pllMachineGroups->push_back(group);
        }
        if (pllMachineGroups->size() == 0)
            pllMachineGroups->push_back("/");
    }
    else
        pllMachineGroups->push_back("/");

    return S_OK;
}

/**
 * Searches for a Medium object with the given ID in the list of registered
 * hard disks.
 *
 * @param aId           ID of the hard disk. Must not be empty.
 * @param aSetError     If @c true , the appropriate error info is set in case
 *                      when the hard disk is not found.
 * @param aHardDisk     Where to store the found hard disk object (can be NULL).
 *
 * @return S_OK, E_INVALIDARG or VBOX_E_OBJECT_NOT_FOUND when not found.
 *
 * @note Locks the media tree for reading.
 */
HRESULT VirtualBox::i_findHardDiskById(const Guid &aId,
                                       bool aSetError,
                                       ComObjPtr<Medium> *aHardDisk /*= NULL*/)
{
    AssertReturn(!aId.isZero(), E_INVALIDARG);

    // we use the hard disks map, but it is protected by the
    // hard disk _list_ lock handle
    AutoReadLock alock(m->allHardDisks.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    HardDiskMap::const_iterator it = m->mapHardDisks.find(aId);
    if (it != m->mapHardDisks.end())
    {
        if (aHardDisk)
            *aHardDisk = (*it).second;
        return S_OK;
    }

    if (aSetError)
        return setError(VBOX_E_OBJECT_NOT_FOUND,
                        tr("Could not find an open hard disk with UUID {%RTuuid}"),
                        aId.raw());

    return VBOX_E_OBJECT_NOT_FOUND;
}

/**
 * Searches for a Medium object with the given ID or location in the list of
 * registered hard disks. If both ID and location are specified, the first
 * object that matches either of them (not necessarily both) is returned.
 *
 * @param strLocation   Full location specification. Must not be empty.
 * @param aSetError     If @c true , the appropriate error info is set in case
 *                      when the hard disk is not found.
 * @param aHardDisk     Where to store the found hard disk object (can be NULL).
 *
 * @return S_OK, E_INVALIDARG or VBOX_E_OBJECT_NOT_FOUND when not found.
 *
 * @note Locks the media tree for reading.
 */
HRESULT VirtualBox::i_findHardDiskByLocation(const Utf8Str &strLocation,
                                             bool aSetError,
                                             ComObjPtr<Medium> *aHardDisk /*= NULL*/)
{
    AssertReturn(!strLocation.isEmpty(), E_INVALIDARG);

    // we use the hard disks map, but it is protected by the
    // hard disk _list_ lock handle
    AutoReadLock alock(m->allHardDisks.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (HardDiskMap::const_iterator it = m->mapHardDisks.begin();
         it != m->mapHardDisks.end();
         ++it)
    {
        const ComObjPtr<Medium> &pHD = (*it).second;

        AutoCaller autoCaller(pHD);
        if (FAILED(autoCaller.hrc())) return autoCaller.hrc();
        AutoWriteLock mlock(pHD COMMA_LOCKVAL_SRC_POS);

        Utf8Str strLocationFull = pHD->i_getLocationFull();

        if (0 == RTPathCompare(strLocationFull.c_str(), strLocation.c_str()))
        {
            if (aHardDisk)
                *aHardDisk = pHD;
            return S_OK;
        }
    }

    if (aSetError)
        return setError(VBOX_E_OBJECT_NOT_FOUND,
                        tr("Could not find an open hard disk with location '%s'"),
                        strLocation.c_str());

    return VBOX_E_OBJECT_NOT_FOUND;
}

/**
 * Searches for a Medium object with the given ID or location in the list of
 * registered DVD or floppy images, depending on the @a mediumType argument.
 * If both ID and file path are specified, the first object that matches either
 * of them (not necessarily both) is returned.
 *
 * @param mediumType Must be either DeviceType_DVD or DeviceType_Floppy.
 * @param aId       ID of the image file (unused when NULL).
 * @param aLocation Full path to the image file (unused when NULL).
 * @param aSetError If @c true, the appropriate error info is set in case when
 *                  the image is not found.
 * @param aImage    Where to store the found image object (can be NULL).
 *
 * @return S_OK when found or E_INVALIDARG or VBOX_E_OBJECT_NOT_FOUND when not found.
 *
 * @note Locks the media tree for reading.
 */
HRESULT VirtualBox::i_findDVDOrFloppyImage(DeviceType_T mediumType,
                                           const Guid *aId,
                                           const Utf8Str &aLocation,
                                           bool aSetError,
                                           ComObjPtr<Medium> *aImage /* = NULL */)
{
    AssertReturn(aId || !aLocation.isEmpty(), E_INVALIDARG);

    Utf8Str location;
    if (!aLocation.isEmpty())
    {
        int vrc = i_calculateFullPath(aLocation, location);
        if (RT_FAILURE(vrc))
            return setError(VBOX_E_FILE_ERROR,
                            tr("Invalid image file location '%s' (%Rrc)"),
                            aLocation.c_str(),
                            vrc);
    }

    MediaOList *pMediaList;

    switch (mediumType)
    {
        case DeviceType_DVD:
            pMediaList = &m->allDVDImages;
        break;

        case DeviceType_Floppy:
            pMediaList = &m->allFloppyImages;
        break;

        default:
            return E_INVALIDARG;
    }

    AutoReadLock alock(pMediaList->getLockHandle() COMMA_LOCKVAL_SRC_POS);

    bool found = false;

    for (MediaList::const_iterator it = pMediaList->begin();
         it != pMediaList->end();
         ++it)
    {
        // no AutoCaller, registered image life time is bound to this
        Medium *pMedium = *it;
        AutoReadLock imageLock(pMedium COMMA_LOCKVAL_SRC_POS);
        const Utf8Str &strLocationFull = pMedium->i_getLocationFull();

        found =     (    aId
                      && pMedium->i_getId() == *aId)
                 || (    !aLocation.isEmpty()
                      && RTPathCompare(location.c_str(),
                                       strLocationFull.c_str()) == 0);
        if (found)
        {
            if (pMedium->i_getDeviceType() != mediumType)
            {
                if (mediumType == DeviceType_DVD)
                    return setError(E_INVALIDARG,
                                    tr("Cannot mount DVD medium '%s' as floppy"), strLocationFull.c_str());
                else
                    return setError(E_INVALIDARG,
                                    tr("Cannot mount floppy medium '%s' as DVD"), strLocationFull.c_str());
            }

            if (aImage)
                *aImage = pMedium;
            break;
        }
    }

    HRESULT hrc = found ? S_OK : VBOX_E_OBJECT_NOT_FOUND;

    if (aSetError && !found)
    {
        if (aId)
            setError(hrc,
                     tr("Could not find an image file with UUID {%RTuuid} in the media registry ('%s')"),
                     aId->raw(),
                     m->strSettingsFilePath.c_str());
        else
            setError(hrc,
                     tr("Could not find an image file with location '%s' in the media registry ('%s')"),
                     aLocation.c_str(),
                     m->strSettingsFilePath.c_str());
    }

    return hrc;
}

/**
 * Searches for an IMedium object that represents the given UUID.
 *
 * If the UUID is empty (indicating an empty drive), this sets pMedium
 * to NULL and returns S_OK.
 *
 * If the UUID refers to a host drive of the given device type, this
 * sets pMedium to the object from the list in IHost and returns S_OK.
 *
 * If the UUID is an image file, this sets pMedium to the object that
 * findDVDOrFloppyImage() returned.
 *
 * If none of the above apply, this returns VBOX_E_OBJECT_NOT_FOUND.
 *
 * @param mediumType Must be DeviceType_DVD or DeviceType_Floppy.
 * @param uuid UUID to search for; must refer to a host drive or an image file or be null.
 * @param fRefresh Whether to refresh the list of host drives in IHost (see Host::getDrives())
 * @param aSetError
 * @param pMedium out: IMedium object found.
 * @return
 */
HRESULT VirtualBox::i_findRemoveableMedium(DeviceType_T mediumType,
                                           const Guid &uuid,
                                           bool fRefresh,
                                           bool aSetError,
                                           ComObjPtr<Medium> &pMedium)
{
    if (uuid.isZero())
    {
        // that's easy
        pMedium.setNull();
        return S_OK;
    }
    else if (!uuid.isValid())
    {
        /* handling of case invalid GUID */
        return setError(VBOX_E_OBJECT_NOT_FOUND,
                            tr("Guid '%s' is invalid"),
                            uuid.toString().c_str());
    }

    // first search for host drive with that UUID
    HRESULT hrc = m->pHost->i_findHostDriveById(mediumType, uuid, fRefresh, pMedium);
    if (hrc == VBOX_E_OBJECT_NOT_FOUND)
                // then search for an image with that UUID
        hrc = i_findDVDOrFloppyImage(mediumType, &uuid, Utf8Str::Empty, aSetError, &pMedium);

    return hrc;
}

/**
 *  Searches for a shared folder with the given logical name
 *  in the collection of shared folders.
 *
 *  @param aName            logical name of the shared folder
 *  @param aSharedFolder    where to return the found object
 *
 *  @return S_OK, E_INVALIDARG or VBOX_E_OBJECT_NOT_FOUND when not found.
 *
 *  @note must be called from under the object's lock
 */
HRESULT VirtualBox::i_findSharedFolder(const Utf8Str &aName,
                                       ComPtr<ISharedFolder> &aSharedFolder)
{
    ComObjPtr<SharedFolder> found;

    AutoReadLock alock(m->allSharedFolders.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (SharedFoldersOList::const_iterator it = m->allSharedFolders.begin();
        it != m->allSharedFolders.end();
        ++it)
    {
        Bstr bstrSharedFolderName;
        HRESULT hrc = (*it)->COMGETTER(Name)(bstrSharedFolderName.asOutParam());
        if (FAILED(hrc)) return hrc;

        if (Utf8Str(bstrSharedFolderName) == aName)
        {
            found = *it;
            break;
        }
    }
    if (!found)
        return VBOX_E_OBJECT_NOT_FOUND;
    return found.queryInterfaceTo(aSharedFolder.asOutParam());
}

/* Look for a GuestOSType object */
HRESULT VirtualBox::i_findGuestOSType(const Utf8Str &strOSType,
                                      ComObjPtr<GuestOSType> &guestOSType)
{
    guestOSType.setNull();

    AssertMsg(m->allGuestOSTypes.size() != 0,
              ("Guest OS types array must be filled"));

    AutoReadLock alock(m->allGuestOSTypes.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    for (GuestOSTypesOList::const_iterator it = m->allGuestOSTypes.begin();
         it != m->allGuestOSTypes.end();
         ++it)
    {
        const Utf8Str &typeId = (*it)->i_id();
        AssertMsg(!typeId.isEmpty(), ("ID must not be NULL"));
        if (strOSType.compare(typeId, Utf8Str::CaseInsensitive) == 0)
        {
            guestOSType = *it;
            return S_OK;
        }
    }

    return setError(VBOX_E_OBJECT_NOT_FOUND,
                    tr("'%s' is not a valid Guest OS type"),
                    strOSType.c_str());
}

/**
 * Walk the list of GuestOSType objects and return a list of guest OS
 * subtypes which correspond to the supplied guest OS family ID.
 *
 * @param strOSFamily    Guest OS family ID.
 * @param aOSSubtypes    Where to store the list of guest OS subtypes.
 *
 * @note Locks the guest OS types list for reading.
 */
HRESULT VirtualBox::getGuestOSSubtypesByFamilyId(const Utf8Str &strOSFamily,
                                                 std::vector<com::Utf8Str> &aOSSubtypes)
{
    std::list<com::Utf8Str> allOSSubtypes;

    AutoReadLock alock(m->allGuestOSTypes.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    bool fFoundGuestOSType = false;
    for (GuestOSTypesOList::const_iterator it = m->allGuestOSTypes.begin();
         it != m->allGuestOSTypes.end(); ++it)
    {
        const Utf8Str &familyId = (*it)->i_familyId();
        AssertMsg(!familyId.isEmpty(), ("familyId must not be NULL"));
        if (familyId.compare(strOSFamily, Utf8Str::CaseInsensitive) == 0)
        {
            fFoundGuestOSType = true;
            break;
        }
    }

    if (!fFoundGuestOSType)
       return setError(VBOX_E_OBJECT_NOT_FOUND,
                       tr("'%s' is not a valid guest OS family identifier."), strOSFamily.c_str());

    for (GuestOSTypesOList::const_iterator it = m->allGuestOSTypes.begin();
         it != m->allGuestOSTypes.end(); ++it)
    {
        const Utf8Str &familyId = (*it)->i_familyId();
        AssertMsg(!familyId.isEmpty(), ("familyId must not be NULL"));
        if (familyId.compare(strOSFamily, Utf8Str::CaseInsensitive) == 0)
        {
            const Utf8Str &strOSSubtype = (*it)->i_subtype();
            if (!strOSSubtype.isEmpty())
                allOSSubtypes.push_back(strOSSubtype);
        }
    }

    /* throw out any duplicates */
    allOSSubtypes.sort();
    allOSSubtypes.unique();

    aOSSubtypes.resize(allOSSubtypes.size());
    size_t i = 0;
    for (std::list<com::Utf8Str>::const_iterator it = allOSSubtypes.begin();
         it != allOSSubtypes.end(); ++it, ++i)
        aOSSubtypes[i] = (*it);

    return S_OK;
}

/**
 * Walk the list of GuestOSType objects and return a list of guest OS
 * descriptions which correspond to the supplied guest OS subtype.
 *
 * @param strOSSubtype     Guest OS subtype.
 * @param aGuestOSDescs    Where to store the list of guest OS descriptions..
 *
 * @note Locks the guest OS types list for reading.
 */
HRESULT VirtualBox::getGuestOSDescsBySubtype(const Utf8Str &strOSSubtype,
                                             std::vector<com::Utf8Str> &aGuestOSDescs)
{
    std::list<com::Utf8Str> allOSDescs;

    AutoReadLock alock(m->allGuestOSTypes.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    bool fFoundGuestOSSubtype = false;
    for (GuestOSTypesOList::const_iterator it = m->allGuestOSTypes.begin();
         it != m->allGuestOSTypes.end(); ++it)
    {
        const Utf8Str &guestOSSubtype = (*it)->i_subtype();
        /* Only some guest OS types have a populated subtype value. */
        if (guestOSSubtype.isNotEmpty() &&
            guestOSSubtype.compare(strOSSubtype, Utf8Str::CaseInsensitive) == 0)
        {
            fFoundGuestOSSubtype = true;
            break;
        }
    }

    if (!fFoundGuestOSSubtype)
       return setError(VBOX_E_OBJECT_NOT_FOUND,
                       tr("'%s' is not a valid guest OS subtype."), strOSSubtype.c_str());

    for (GuestOSTypesOList::const_iterator it = m->allGuestOSTypes.begin();
         it != m->allGuestOSTypes.end(); ++it)
    {
        const Utf8Str &guestOSSubtype = (*it)->i_subtype();
        /* Only some guest OS types have a populated subtype value. */
        if (guestOSSubtype.isNotEmpty() &&
            guestOSSubtype.compare(strOSSubtype, Utf8Str::CaseInsensitive) == 0)
        {
            const Utf8Str &strOSDesc = (*it)->i_description();
            allOSDescs.push_back(strOSDesc);
        }
    }

    aGuestOSDescs.resize(allOSDescs.size());
    size_t i = 0;
    for (std::list<com::Utf8Str>::const_iterator it = allOSDescs.begin();
         it != allOSDescs.end(); ++it, ++i)
        aGuestOSDescs[i] = (*it);

    return S_OK;
}

/**
 * Returns the constant pseudo-machine UUID that is used to identify the
 * global media registry.
 *
 * Starting with VirtualBox 4.0 each medium remembers in its instance data
 * in which media registry it is saved (if any): this can either be a machine
 * UUID, if it's in a per-machine media registry, or this global ID.
 *
 * This UUID is only used to identify the VirtualBox object while VirtualBox
 * is running. It is a compile-time constant and not saved anywhere.
 *
 * @return
 */
const Guid& VirtualBox::i_getGlobalRegistryId() const
{
    return m->uuidMediaRegistry;
}

const ComObjPtr<Host>& VirtualBox::i_host() const
{
    return m->pHost;
}

SystemProperties* VirtualBox::i_getSystemProperties() const
{
    return m->pSystemProperties;
}

CloudProviderManager *VirtualBox::i_getCloudProviderManager() const
{
    return m->pCloudProviderManager;
}

#ifdef VBOX_WITH_EXTPACK
/**
 * Getter that SystemProperties and others can use to talk to the extension
 * pack manager.
 */
ExtPackManager* VirtualBox::i_getExtPackManager() const
{
    return m->ptrExtPackManager;
}
#endif

/**
 * Getter that machines can talk to the autostart database.
 */
AutostartDb* VirtualBox::i_getAutostartDb() const
{
    return m->pAutostartDb;
}

#ifdef VBOX_WITH_RESOURCE_USAGE_API
const ComObjPtr<PerformanceCollector>& VirtualBox::i_performanceCollector() const
{
    return m->pPerformanceCollector;
}
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

/**
 * Returns the default machine folder from the system properties
 * with proper locking.
 */
void VirtualBox::i_getDefaultMachineFolder(Utf8Str &str) const
{
    AutoReadLock propsLock(m->pSystemProperties COMMA_LOCKVAL_SRC_POS);
    str = m->pSystemProperties->m->strDefaultMachineFolder;
}

/**
 * Returns the default hard disk format from the system properties
 * with proper locking.
 */
void VirtualBox::i_getDefaultHardDiskFormat(Utf8Str &str) const
{
    AutoReadLock propsLock(m->pSystemProperties COMMA_LOCKVAL_SRC_POS);
    str = m->pSystemProperties->m->strDefaultHardDiskFormat;
}

const Utf8Str& VirtualBox::i_homeDir() const
{
    return m->strHomeDir;
}

/**
 * Calculates the absolute path of the given path taking the VirtualBox home
 * directory as the current directory.
 *
 * @param  strPath  Path to calculate the absolute path for.
 * @param  aResult  Where to put the result (used only on success, can be the
 *                  same Utf8Str instance as passed in @a aPath).
 * @return IPRT result.
 *
 * @note Doesn't lock any object.
 */
int VirtualBox::i_calculateFullPath(const Utf8Str &strPath, Utf8Str &aResult)
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), VERR_GENERAL_FAILURE);

    /* no need to lock since strHomeDir is const */

    char szFolder[RTPATH_MAX];
    size_t cbFolder = sizeof(szFolder);
    int vrc = RTPathAbsEx(m->strHomeDir.c_str(),
                          strPath.c_str(),
                          RTPATH_STR_F_STYLE_HOST,
                          szFolder,
                          &cbFolder);
    if (RT_SUCCESS(vrc))
        aResult = szFolder;

    return vrc;
}

/**
 * Copies strSource to strTarget, making it relative to the VirtualBox config folder
 * if it is a subdirectory thereof, or simply copying it otherwise.
 *
 * @param strSource Path to evalue and copy.
 * @param strTarget Buffer to receive target path.
 */
void VirtualBox::i_copyPathRelativeToConfig(const Utf8Str &strSource,
                                            Utf8Str &strTarget)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    // no need to lock since mHomeDir is const

    // use strTarget as a temporary buffer to hold the machine settings dir
    strTarget = m->strHomeDir;
    if (RTPathStartsWith(strSource.c_str(), strTarget.c_str()))
        // is relative: then append what's left
        strTarget.append(strSource.c_str() + strTarget.length());     // include '/'
    else
        // is not relative: then overwrite
        strTarget = strSource;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Checks if there is a hard disk, DVD or floppy image with the given ID or
 * location already registered.
 *
 * On return, sets @a aConflict to the string describing the conflicting medium,
 * or sets it to @c Null if no conflicting media is found. Returns S_OK in
 * either case. A failure is unexpected.
 *
 * @param aId           UUID to check.
 * @param aLocation     Location to check.
 * @param aConflict     Where to return parameters of the conflicting medium.
 * @param ppMedium      Medium reference in case this is simply a duplicate.
 *
 * @note Locks the media tree and media objects for reading.
 */
HRESULT VirtualBox::i_checkMediaForConflicts(const Guid &aId,
                                             const Utf8Str &aLocation,
                                             Utf8Str &aConflict,
                                             ComObjPtr<Medium> *ppMedium)
{
    AssertReturn(!aId.isZero() && !aLocation.isEmpty(), E_FAIL);
    AssertReturn(ppMedium, E_INVALIDARG);

    aConflict.setNull();
    ppMedium->setNull();

    AutoReadLock alock(i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    ComObjPtr<Medium> pMediumFound;
    const char *pcszType = NULL;

    if (aId.isValid() && !aId.isZero())
        hrc = i_findHardDiskById(aId, false /* aSetError */, &pMediumFound);
    if (FAILED(hrc) && !aLocation.isEmpty())
        hrc = i_findHardDiskByLocation(aLocation, false /* aSetError */, &pMediumFound);
    if (SUCCEEDED(hrc))
        pcszType = tr("hard disk");

    if (!pcszType)
    {
        hrc = i_findDVDOrFloppyImage(DeviceType_DVD, &aId, aLocation, false /* aSetError */, &pMediumFound);
        if (SUCCEEDED(hrc))
            pcszType = tr("CD/DVD image");
    }

    if (!pcszType)
    {
        hrc = i_findDVDOrFloppyImage(DeviceType_Floppy, &aId, aLocation, false /* aSetError */, &pMediumFound);
        if (SUCCEEDED(hrc))
            pcszType = tr("floppy image");
    }

    if (pcszType && pMediumFound)
    {
        /* Note: no AutoCaller since bound to this */
        AutoReadLock mlock(pMediumFound COMMA_LOCKVAL_SRC_POS);

        Utf8Str strLocFound = pMediumFound->i_getLocationFull();
        Guid idFound = pMediumFound->i_getId();

        if (    (RTPathCompare(strLocFound.c_str(), aLocation.c_str()) == 0)
             && (idFound == aId)
           )
            *ppMedium = pMediumFound;

        aConflict = Utf8StrFmt(tr("%s '%s' with UUID {%RTuuid}"),
                               pcszType,
                               strLocFound.c_str(),
                               idFound.raw());
    }

    return S_OK;
}

/**
 * Checks whether the given UUID is already in use by one medium for the
 * given device type.
 *
 * @returns true if the UUID is already in use
 *          fale otherwise
 * @param   aId           The UUID to check.
 * @param   deviceType    The device type the UUID is going to be checked for
 *                        conflicts.
 */
bool VirtualBox::i_isMediaUuidInUse(const Guid &aId, DeviceType_T deviceType)
{
    /* A zero UUID is invalid here, always claim that it is already used. */
    AssertReturn(!aId.isZero(), true);

    AutoReadLock alock(i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    bool fInUse = false;

    ComObjPtr<Medium> pMediumFound;

    HRESULT hrc;
    switch (deviceType)
    {
        case DeviceType_HardDisk:
            hrc = i_findHardDiskById(aId, false /* aSetError */, &pMediumFound);
            break;
        case DeviceType_DVD:
            hrc = i_findDVDOrFloppyImage(DeviceType_DVD, &aId, Utf8Str::Empty, false /* aSetError */, &pMediumFound);
            break;
        case DeviceType_Floppy:
            hrc = i_findDVDOrFloppyImage(DeviceType_Floppy, &aId, Utf8Str::Empty, false /* aSetError */, &pMediumFound);
            break;
        default:
            AssertMsgFailed(("Invalid device type %d\n", deviceType));
            hrc = S_OK;
            break;
    }

    if (SUCCEEDED(hrc) && pMediumFound)
        fInUse = true;

    return fInUse;
}

/**
 * Called from Machine::prepareSaveSettings() when it has detected
 * that a machine has been renamed. Such renames will require
 * updating the global media registry during the
 * VirtualBox::i_saveSettings() that follows later.
*
 * When a machine is renamed, there may well be media (in particular,
 * diff images for snapshots) in the global registry that will need
 * to have their paths updated. Before 3.2, Machine::saveSettings
 * used to call VirtualBox::i_saveSettings implicitly, which was both
 * unintuitive and caused locking order problems. Now, we remember
 * such pending name changes with this method so that
 * VirtualBox::i_saveSettings() can process them properly.
 */
void VirtualBox::i_rememberMachineNameChangeForMedia(const Utf8Str &strOldConfigDir,
                                                     const Utf8Str &strNewConfigDir)
{
    AutoWriteLock mediaLock(i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    Data::PendingMachineRename pmr;
    pmr.strConfigDirOld = strOldConfigDir;
    pmr.strConfigDirNew = strNewConfigDir;
    m->llPendingMachineRenames.push_back(pmr);
}

static DECLCALLBACK(int) fntSaveMediaRegistries(void *pvUser);

class SaveMediaRegistriesDesc : public ThreadTask
{

public:
    SaveMediaRegistriesDesc()
    {
        m_strTaskName = "SaveMediaReg";
    }
    virtual ~SaveMediaRegistriesDesc(void) { }

private:
    void handler()
    {
        try
        {
            fntSaveMediaRegistries(this);
        }
        catch(...)
        {
            LogRel(("Exception in the function fntSaveMediaRegistries()\n"));
        }
    }

    MediaList llMedia;
    ComObjPtr<VirtualBox> pVirtualBox;

    friend DECLCALLBACK(int) fntSaveMediaRegistries(void *pvUser);
    friend void VirtualBox::i_saveMediaRegistry(settings::MediaRegistry &mediaRegistry,
                                                const Guid &uuidRegistry,
                                                const Utf8Str &strMachineFolder);
};

DECLCALLBACK(int) fntSaveMediaRegistries(void *pvUser)
{
    SaveMediaRegistriesDesc *pDesc = (SaveMediaRegistriesDesc *)pvUser;
    if (!pDesc)
    {
        LogRelFunc(("Thread for saving media registries lacks parameters\n"));
        return VERR_INVALID_PARAMETER;
    }

    for (MediaList::const_iterator it = pDesc->llMedia.begin();
         it != pDesc->llMedia.end();
         ++it)
    {
        Medium *pMedium = *it;
        pMedium->i_markRegistriesModified();
    }

    pDesc->pVirtualBox->i_saveModifiedRegistries();

    pDesc->llMedia.clear();
    pDesc->pVirtualBox.setNull();

    return VINF_SUCCESS;
}

/**
 * Goes through all known media (hard disks, floppies and DVDs) and saves
 * those into the given settings::MediaRegistry structures whose registry
 * ID match the given UUID.
 *
 * Before actually writing to the structures, all media paths (not just the
 * ones for the given registry) are updated if machines have been renamed
 * since the last call.
 *
 * This gets called from two contexts:
 *
 *  -- VirtualBox::i_saveSettings() with the UUID of the global registry
 *     (VirtualBox::Data.uuidRegistry); this will save those media
 *     which had been loaded from the global registry or have been
 *     attached to a "legacy" machine which can't save its own registry;
 *
 *  -- Machine::saveSettings() with the UUID of a machine, if a medium
 *     has been attached to a machine created with VirtualBox 4.0 or later.
 *
 * Media which have only been temporarily opened without having been
 * attached to a machine have a NULL registry UUID and therefore don't
 * get saved.
 *
 * This locks the media tree. Throws HRESULT on errors!
 *
 * @param mediaRegistry Settings structure to fill.
 * @param uuidRegistry The UUID of the media registry; either a machine UUID
 *        (if machine registry) or the UUID of the global registry.
 * @param strMachineFolder The machine folder for relative paths, if machine registry, or an empty string otherwise.
 */
void VirtualBox::i_saveMediaRegistry(settings::MediaRegistry &mediaRegistry,
                                     const Guid &uuidRegistry,
                                     const Utf8Str &strMachineFolder)
{
    // lock all media for the following; use a write lock because we're
    // modifying the PendingMachineRenamesList, which is protected by this
    AutoWriteLock mediaLock(i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

    // if a machine was renamed, then we'll need to refresh media paths
    if (m->llPendingMachineRenames.size())
    {
        // make a single list from the three media lists so we don't need three loops
        MediaList llAllMedia;
        // with hard disks, we must use the map, not the list, because the list only has base images
        for (HardDiskMap::iterator it = m->mapHardDisks.begin(); it != m->mapHardDisks.end(); ++it)
            llAllMedia.push_back(it->second);
        for (MediaList::iterator it = m->allDVDImages.begin(); it != m->allDVDImages.end(); ++it)
            llAllMedia.push_back(*it);
        for (MediaList::iterator it = m->allFloppyImages.begin(); it != m->allFloppyImages.end(); ++it)
            llAllMedia.push_back(*it);

        SaveMediaRegistriesDesc *pDesc = new SaveMediaRegistriesDesc();
        for (MediaList::iterator it = llAllMedia.begin();
             it != llAllMedia.end();
             ++it)
        {
            Medium *pMedium = *it;
            for (Data::PendingMachineRenamesList::iterator it2 = m->llPendingMachineRenames.begin();
                 it2 != m->llPendingMachineRenames.end();
                 ++it2)
            {
                const Data::PendingMachineRename &pmr = *it2;
                HRESULT hrc = pMedium->i_updatePath(pmr.strConfigDirOld, pmr.strConfigDirNew);
                if (SUCCEEDED(hrc))
                {
                    // Remember which medium objects has been changed,
                    // to trigger saving their registries later.
                    pDesc->llMedia.push_back(pMedium);
                } else if (hrc == VBOX_E_FILE_ERROR)
                    /* nothing */;
                else
                    AssertComRC(hrc);
            }
        }
        // done, don't do it again until we have more machine renames
        m->llPendingMachineRenames.clear();

        if (pDesc->llMedia.size())
        {
            // Handle the media registry saving in a separate thread, to
            // avoid giant locking problems and passing up the list many
            // levels up to whoever triggered saveSettings, as there are
            // lots of places which would need to handle saving more settings.
            pDesc->pVirtualBox = this;

            //the function createThread() takes ownership of pDesc
            //so there is no need to use delete operator for pDesc
            //after calling this function
            HRESULT hrc = pDesc->createThread();
            pDesc = NULL;

            if (FAILED(hrc))
            {
                // failure means that settings aren't saved, but there isn't
                // much we can do besides avoiding memory leaks
                LogRelFunc(("Failed to create thread for saving media registries (%Rhr)\n", hrc));
            }
        }
        else
            delete pDesc;
    }

    struct {
        MediaOList &llSource;
        settings::MediaList &llTarget;
    } s[] =
    {
        // hard disks
        { m->allHardDisks, mediaRegistry.llHardDisks },
        // CD/DVD images
        { m->allDVDImages, mediaRegistry.llDvdImages },
        // floppy images
        { m->allFloppyImages, mediaRegistry.llFloppyImages }
    };

    for (size_t i = 0; i < RT_ELEMENTS(s); ++i)
    {
        MediaOList &llSource = s[i].llSource;
        settings::MediaList &llTarget = s[i].llTarget;
        llTarget.clear();
        for (MediaList::const_iterator it = llSource.begin();
             it != llSource.end();
             ++it)
        {
            Medium *pMedium = *it;
            AutoCaller autoCaller(pMedium);
            if (FAILED(autoCaller.hrc())) throw autoCaller.hrc();
            AutoReadLock mlock(pMedium COMMA_LOCKVAL_SRC_POS);

            if (pMedium->i_isInRegistry(uuidRegistry))
            {
                llTarget.push_back(settings::Medium::Empty);
                HRESULT hrc = pMedium->i_saveSettings(llTarget.back(), strMachineFolder); // this recurses into child hard disks
                if (FAILED(hrc))
                {
                    llTarget.pop_back();
                    throw hrc;
                }
            }
        }
    }
}

/**
 *  Helper function which actually writes out VirtualBox.xml, the main configuration file.
 *  Gets called from the public VirtualBox::SaveSettings() as well as from various other
 *  places internally when settings need saving.
 *
 *  @note Caller must have locked the VirtualBox object for writing and must not hold any
 *    other locks since this locks all kinds of member objects and trees temporarily,
 *    which could cause conflicts.
 */
HRESULT VirtualBox::i_saveSettings()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AssertReturn(isWriteLockOnCurrentThread(), E_FAIL);
    AssertReturn(!m->strSettingsFilePath.isEmpty(), E_FAIL);

    i_unmarkRegistryModified(i_getGlobalRegistryId());

    HRESULT hrc = S_OK;

    try
    {
        // machines
        m->pMainConfigFile->llMachines.clear();
        {
            AutoReadLock machinesLock(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);
            for (MachinesOList::iterator it = m->allMachines.begin();
                 it != m->allMachines.end();
                 ++it)
            {
                Machine *pMachine = *it;
                // save actual machine registry entry
                settings::MachineRegistryEntry mre;
                hrc = pMachine->i_saveRegistryEntry(mre);
                if (FAILED(hrc)) throw hrc;
                m->pMainConfigFile->llMachines.push_back(mre);
            }
        }

        i_saveMediaRegistry(m->pMainConfigFile->mediaRegistry,
                            m->uuidMediaRegistry,         // global media registry ID
                            Utf8Str::Empty);              // strMachineFolder

        m->pMainConfigFile->llDhcpServers.clear();
        {
            AutoReadLock dhcpLock(m->allDHCPServers.getLockHandle() COMMA_LOCKVAL_SRC_POS);
            for (DHCPServersOList::const_iterator it = m->allDHCPServers.begin();
                 it != m->allDHCPServers.end();
                 ++it)
            {
                settings::DHCPServer d;
                hrc = (*it)->i_saveSettings(d);
                if (FAILED(hrc)) throw hrc;
                m->pMainConfigFile->llDhcpServers.push_back(d);
            }
        }
        m->pMainConfigFile->llGlobalSharedFolders.clear();
        {
            AutoReadLock sharedFolderLock(m->allSharedFolders.getLockHandle() COMMA_LOCKVAL_SRC_POS);
            for (SharedFoldersOList::const_iterator it = m->allSharedFolders.begin();
                 it != m->allSharedFolders.end();
                 ++it)
            {
                settings::SharedFolder sf;
                hrc = (*it)->i_saveSettings(sf);
                if (FAILED(hrc)) throw hrc;
                m->pMainConfigFile->llGlobalSharedFolders.push_back(sf);
            }
        }

#ifdef VBOX_WITH_NAT_SERVICE
        /* Saving NAT Network configuration */
        m->pMainConfigFile->llNATNetworks.clear();
        {
            AutoReadLock natNetworkLock(m->allNATNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
            for (NATNetworksOList::const_iterator it = m->allNATNetworks.begin();
                 it != m->allNATNetworks.end();
                 ++it)
            {
                settings::NATNetwork n;
                hrc = (*it)->i_saveSettings(n);
                if (FAILED(hrc)) throw hrc;
                m->pMainConfigFile->llNATNetworks.push_back(n);
            }
        }
#endif

#ifdef VBOX_WITH_VMNET
        m->pMainConfigFile->llHostOnlyNetworks.clear();
        {
            AutoReadLock hostOnlyNetworkLock(m->allHostOnlyNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
            for (HostOnlyNetworksOList::const_iterator it = m->allHostOnlyNetworks.begin();
                 it != m->allHostOnlyNetworks.end();
                 ++it)
            {
                settings::HostOnlyNetwork n;
                hrc = (*it)->i_saveSettings(n);
                if (FAILED(hrc)) throw hrc;
                m->pMainConfigFile->llHostOnlyNetworks.push_back(n);
            }
        }
#endif /* VBOX_WITH_VMNET */

#ifdef VBOX_WITH_CLOUD_NET
        m->pMainConfigFile->llCloudNetworks.clear();
        {
            AutoReadLock cloudNetworkLock(m->allCloudNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);
            for (CloudNetworksOList::const_iterator it = m->allCloudNetworks.begin();
                 it != m->allCloudNetworks.end();
                 ++it)
            {
                settings::CloudNetwork n;
                hrc = (*it)->i_saveSettings(n);
                if (FAILED(hrc)) throw hrc;
                m->pMainConfigFile->llCloudNetworks.push_back(n);
            }
        }
#endif /* VBOX_WITH_CLOUD_NET */
        // leave extra data alone, it's still in the config file

        // host data (USB filters)
        hrc = m->pHost->i_saveSettings(m->pMainConfigFile->host);
        if (FAILED(hrc)) throw hrc;

        hrc = m->pSystemProperties->i_saveSettings(m->pMainConfigFile->systemProperties);
        if (FAILED(hrc)) throw hrc;

        // and write out the XML, still under the lock
        m->pMainConfigFile->write(m->strSettingsFilePath);
    }
    catch (HRESULT hrcXcpt)
    {
        /* we assume that error info is set by the thrower */
        hrc = hrcXcpt;
    }
    catch (...)
    {
        hrc = VirtualBoxBase::handleUnexpectedExceptions(this, RT_SRC_POS);
    }

    return hrc;
}

/**
 *  Helper to register the machine.
 *
 *  When called during VirtualBox startup, adds the given machine to the
 *  collection of registered machines. Otherwise tries to mark the machine
 *  as registered, and, if succeeded, adds it to the collection and
 *  saves global settings.
 *
 *  @note The caller must have added itself as a caller of the @a aMachine
 *  object if calls this method not on VirtualBox startup.
 *
 *  @param aMachine     machine to register
 *
 *  @note Locks objects!
 */
HRESULT VirtualBox::i_registerMachine(Machine *aMachine)
{
    ComAssertRet(aMachine, E_INVALIDARG);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    HRESULT hrc = S_OK;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    {
        ComObjPtr<Machine> pMachine;
        hrc = i_findMachine(aMachine->i_getId(),
                            true /* fPermitInaccessible */,
                            false /* aDoSetError */,
                            &pMachine);
        if (SUCCEEDED(hrc))
        {
            /* sanity */
            AutoLimitedCaller machCaller(pMachine);
            AssertComRC(machCaller.hrc());

            return setError(E_INVALIDARG,
                            tr("Registered machine with UUID {%RTuuid} ('%s') already exists"),
                            aMachine->i_getId().raw(),
                            pMachine->i_getSettingsFileFull().c_str());
        }

        ComAssertRet(hrc == VBOX_E_OBJECT_NOT_FOUND, hrc);
        hrc = S_OK;
    }

    if (getObjectState().getState() != ObjectState::InInit)
    {
        hrc = aMachine->i_prepareRegister();
        if (FAILED(hrc)) return hrc;
    }

    /* add to the collection of registered machines */
    m->allMachines.addChild(aMachine);

    if (getObjectState().getState() != ObjectState::InInit)
        hrc = i_saveSettings();

    return hrc;
}

/**
 * Remembers the given medium object by storing it in either the global
 * medium registry or a machine one.
 *
 * @note Caller must hold the media tree lock for writing; in addition, this
 * locks @a pMedium for reading
 *
 * @param pMedium   Medium object to remember.
 * @param ppMedium  Actually stored medium object. Can be different if due
 *                  to an unavoidable race there was a duplicate Medium object
 *                  created.
 * @param mediaTreeLock Reference to the AutoWriteLock holding the media tree
 *                  lock, necessary to release it in the right spot.
 * @param fCalledFromMediumInit Flag whether this is called from Medium::init().
 * @return
 */
HRESULT VirtualBox::i_registerMedium(const ComObjPtr<Medium> &pMedium,
                                     ComObjPtr<Medium> *ppMedium,
                                     AutoWriteLock &mediaTreeLock,
                                     bool fCalledFromMediumInit)
{
    AssertReturn(pMedium != NULL, E_INVALIDARG);
    AssertReturn(ppMedium != NULL, E_INVALIDARG);

    // caller must hold the media tree write lock
    Assert(i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoCaller mediumCaller(pMedium);
    AssertComRCReturnRC(mediumCaller.hrc());

    bool fAddToGlobalRegistry = false;
    const char *pszDevType = NULL;
    Guid regId;
    ObjectsList<Medium> *pall = NULL;
    DeviceType_T devType;
    {
        AutoReadLock mediumLock(pMedium COMMA_LOCKVAL_SRC_POS);
        devType = pMedium->i_getDeviceType();

        if (!pMedium->i_getFirstRegistryMachineId(regId))
            fAddToGlobalRegistry = true;
    }
    switch (devType)
    {
        case DeviceType_HardDisk:
            pall = &m->allHardDisks;
            pszDevType = tr("hard disk");
            break;
        case DeviceType_DVD:
            pszDevType = tr("DVD image");
            pall = &m->allDVDImages;
            break;
        case DeviceType_Floppy:
            pszDevType = tr("floppy image");
            pall = &m->allFloppyImages;
            break;
        default:
            AssertMsgFailedReturn(("invalid device type %d", devType), E_INVALIDARG);
    }

    Guid id;
    Utf8Str strLocationFull;
    ComObjPtr<Medium> pParent;
    {
        AutoReadLock mediumLock(pMedium COMMA_LOCKVAL_SRC_POS);
        id = pMedium->i_getId();
        strLocationFull = pMedium->i_getLocationFull();
        pParent = pMedium->i_getParent();

        /*
         * If a separate thread has called Medium::close() for this medium at the same
         * time as this i_registerMedium() call then there is a window of opportunity in
         * Medium::i_close() where the media tree lock is dropped before calling
         * Medium::uninit() (which reacquires the lock) that we can end up here attempting
         * to register a medium which is in the process of being closed.  In addition, if
         * this is a differencing medium and Medium::close() is in progress for one its
         * parent media then we are similarly operating on a media registry in flux.  In
         * either case registering a medium just before calling Medium::uninit() will
         * lead to an inconsistent media registry so bail out here since Medium::close()
         * got to this medium (or one of its parents) first.
         */
        if (devType == DeviceType_HardDisk)
        {
            ComObjPtr<Medium> pTmpMedium = pMedium;
            while (pTmpMedium.isNotNull())
            {
                AutoCaller mediumAC(pTmpMedium);
                if (FAILED(mediumAC.hrc())) return mediumAC.hrc();
                AutoReadLock mlock(pTmpMedium COMMA_LOCKVAL_SRC_POS);

                if (pTmpMedium->i_isClosing())
                    return setError(E_INVALIDARG,
                                    tr("Cannot register %s '%s' {%RTuuid} because it is in the process of being closed"),
                                    pszDevType,
                                    pTmpMedium->i_getLocationFull().c_str(),
                                    pTmpMedium->i_getId().raw());

                pTmpMedium = pTmpMedium->i_getParent();
            }
        }
    }

    HRESULT hrc;

    Utf8Str strConflict;
    ComObjPtr<Medium> pDupMedium;
    hrc = i_checkMediaForConflicts(id, strLocationFull, strConflict, &pDupMedium);
    if (FAILED(hrc)) return hrc;

    if (pDupMedium.isNull())
    {
        if (strConflict.length())
            return setError(E_INVALIDARG,
                            tr("Cannot register the %s '%s' {%RTuuid} because a %s already exists"),
                            pszDevType,
                            strLocationFull.c_str(),
                            id.raw(),
                            strConflict.c_str(),
                            m->strSettingsFilePath.c_str());

        // add to the collection if it is a base medium
        if (pParent.isNull())
            pall->getList().push_back(pMedium);

        // store all hard disks (even differencing images) in the map
        if (devType == DeviceType_HardDisk)
            m->mapHardDisks[id] = pMedium;
    }

    /*
     * If we have been called from Medium::initFromSettings() then the Medium object's
     * AutoCaller status will be 'InInit' which means that when making the assigment to
     * ppMedium below the Medium object will not call Medium::uninit().  By excluding
     * this code path from releasing and reacquiring the media tree lock we avoid a
     * potential deadlock with other threads which may be operating on the
     * disks/DVDs/floppies in the VM's media registry at the same time such as
     * Machine::unregister().
     */
    if (!fCalledFromMediumInit)
    {
        // pMedium may be the last reference to the Medium object, and the
        // caller may have specified the same ComObjPtr as the output parameter.
        // In this case the assignment will uninit the object, and we must not
        // have a caller pending.
        mediumCaller.release();
        // release media tree lock, must not be held at uninit time.
        mediaTreeLock.release();
        // must not hold the media tree write lock any more
        Assert(!i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());
    }

    *ppMedium = pDupMedium.isNull() ? pMedium : pDupMedium;

    if (fAddToGlobalRegistry)
    {
        AutoWriteLock mediumLock(pMedium COMMA_LOCKVAL_SRC_POS);
        if (  fCalledFromMediumInit
            ? (*ppMedium)->i_addRegistryNoCallerCheck(m->uuidMediaRegistry)
            : (*ppMedium)->i_addRegistry(m->uuidMediaRegistry))
            i_markRegistryModified(m->uuidMediaRegistry);
    }

    // Restore the initial lock state, so that no unexpected lock changes are
    // done by this method, which would need adjustments everywhere.
    if (!fCalledFromMediumInit)
        mediaTreeLock.acquire();

    return hrc;
}

/**
 * Removes the given medium from the respective registry.
 *
 * @param pMedium    Hard disk object to remove.
 *
 * @note Caller must hold the media tree lock for writing; in addition, this locks @a pMedium for reading
 */
HRESULT VirtualBox::i_unregisterMedium(Medium *pMedium)
{
    AssertReturn(pMedium != NULL, E_INVALIDARG);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoCaller mediumCaller(pMedium);
    AssertComRCReturnRC(mediumCaller.hrc());

    // caller must hold the media tree write lock
    Assert(i_getMediaTreeLockHandle().isWriteLockOnCurrentThread());

    Guid id;
    ComObjPtr<Medium> pParent;
    DeviceType_T devType;
    {
        AutoReadLock mediumLock(pMedium COMMA_LOCKVAL_SRC_POS);
        id = pMedium->i_getId();
        pParent = pMedium->i_getParent();
        devType = pMedium->i_getDeviceType();
    }

    ObjectsList<Medium> *pall = NULL;
    switch (devType)
    {
        case DeviceType_HardDisk:
            pall = &m->allHardDisks;
            break;
        case DeviceType_DVD:
            pall = &m->allDVDImages;
            break;
        case DeviceType_Floppy:
            pall = &m->allFloppyImages;
            break;
        default:
            AssertMsgFailedReturn(("invalid device type %d", devType), E_INVALIDARG);
    }

    // remove from the collection if it is a base medium
    if (pParent.isNull())
        pall->getList().remove(pMedium);

    // remove all hard disks (even differencing images) from map
    if (devType == DeviceType_HardDisk)
    {
        size_t cnt = m->mapHardDisks.erase(id);
        Assert(cnt == 1);
        NOREF(cnt);
    }

    return S_OK;
}

/**
 * Unregisters all Medium objects which belong to the given machine registry.
 * Gets called from Machine::uninit() just before the machine object dies
 * and must only be called with a machine UUID as the registry ID.
 *
 * Locks the media tree.
 *
 * @param uuidMachine Medium registry ID (always a machine UUID)
 * @return
 */
HRESULT VirtualBox::i_unregisterMachineMedia(const Guid &uuidMachine)
{
    Assert(!uuidMachine.isZero() && uuidMachine.isValid());

    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    MediaList llMedia2Close;

    {
        AutoWriteLock tlock(i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);

        for (MediaOList::iterator it = m->allHardDisks.getList().begin();
             it != m->allHardDisks.getList().end();
             ++it)
        {
            ComObjPtr<Medium> pMedium = *it;
            AutoCaller medCaller(pMedium);
            if (FAILED(medCaller.hrc())) return medCaller.hrc();
            AutoReadLock medlock(pMedium COMMA_LOCKVAL_SRC_POS);
            Log(("Looking at medium %RTuuid\n", pMedium->i_getId().raw()));

            /* If the medium is still in the registry then either some code is
             * seriously buggy (unregistering a VM removes it automatically),
             * or the reference to a Machine object is destroyed without ever
             * being registered. The second condition checks if a medium is
             * in no registry, which indicates (set by unregistering) that a
             * medium is not used by any other VM and thus can be closed. */
            Guid dummy;
            if (   pMedium->i_isInRegistry(uuidMachine)
                || !pMedium->i_getFirstRegistryMachineId(dummy))
            {
                /* Collect all medium objects into llMedia2Close,
                 * in right order for closing. */
                MediaList llMediaTodo;
                llMediaTodo.push_back(pMedium);

                while (llMediaTodo.size() > 0)
                {
                    ComObjPtr<Medium> pCurrent = llMediaTodo.front();
                    llMediaTodo.pop_front();

                    /* Add to front, order must be children then parent. */
                    Log(("Pushing medium %RTuuid (front)\n", pCurrent->i_getId().raw()));
                    llMedia2Close.push_front(pCurrent);

                    /* process all children */
                    MediaList::const_iterator itBegin = pCurrent->i_getChildren().begin();
                    MediaList::const_iterator itEnd = pCurrent->i_getChildren().end();
                    for (MediaList::const_iterator it2 = itBegin; it2 != itEnd; ++it2)
                        llMediaTodo.push_back(*it2);
                }
            }
        }
    }

    for (MediaList::iterator it = llMedia2Close.begin();
         it != llMedia2Close.end();
         ++it)
    {
        ComObjPtr<Medium> pMedium = *it;
        Log(("Closing medium %RTuuid\n", pMedium->i_getId().raw()));
        AutoCaller mac(pMedium);
        HRESULT hrc = pMedium->i_close(mac);
        if (FAILED(hrc))
            return hrc;
    }

    LogFlowFuncLeave();

    return S_OK;
}

/**
 * Removes the given machine object from the internal list of registered machines.
 * Called from Machine::Unregister().
 * @param pMachine
 * @param aCleanupMode  How to handle medium attachments. For
 *      CleanupMode_UnregisterOnly the associated medium objects will be
 *      closed when the Machine object is uninitialized, otherwise they will
 *      go to the global registry if no better registry is found.
 * @param id  UUID of the machine. Must be passed by caller because machine may be dead by this time.
 * @return
 */
HRESULT VirtualBox::i_unregisterMachine(Machine *pMachine,
                                        CleanupMode_T aCleanupMode,
                                        const Guid &id)
{
    // remove from the collection of registered machines
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->allMachines.removeChild(pMachine);
    // save the global registry
    HRESULT hrc = i_saveSettings();
    alock.release();

    /*
     * Now go over all known media and checks if they were registered in the
     * media registry of the given machine. Each such medium is then moved to
     * a different media registry to make sure it doesn't get lost since its
     * media registry is about to go away.
     *
     * This fixes the following use case: Image A.vdi of machine A is also used
     * by machine B, but registered in the media registry of machine A. If machine
     * A is deleted, A.vdi must be moved to the registry of B, or else B will
     * become inaccessible.
     */
    {
        AutoReadLock tlock(i_getMediaTreeLockHandle() COMMA_LOCKVAL_SRC_POS);
        // iterate over the list of *base* images
        for (MediaOList::iterator it = m->allHardDisks.getList().begin();
             it != m->allHardDisks.getList().end();
             ++it)
        {
            ComObjPtr<Medium> &pMedium = *it;
            AutoCaller medCaller(pMedium);
            if (FAILED(medCaller.hrc())) return medCaller.hrc();
            AutoWriteLock mlock(pMedium COMMA_LOCKVAL_SRC_POS);

            if (pMedium->i_removeRegistryAll(id))
            {
                // machine ID was found in base medium's registry list:
                // move this base image and all its children to another registry then
                // 1) first, find a better registry to add things to
                const Guid *puuidBetter = pMedium->i_getAnyMachineBackref(id);
                if (puuidBetter)
                {
                    // 2) better registry found: then use that
                    pMedium->i_addRegistryAll(*puuidBetter);
                    // 3) and make sure the registry is saved below
                    mlock.release();
                    tlock.release();
                    i_markRegistryModified(*puuidBetter);
                    tlock.acquire();
                    mlock.acquire();
                }
                else if (aCleanupMode != CleanupMode_UnregisterOnly)
                {
                    pMedium->i_addRegistryAll(i_getGlobalRegistryId());
                    mlock.release();
                    tlock.release();
                    i_markRegistryModified(i_getGlobalRegistryId());
                    tlock.acquire();
                    mlock.acquire();
                }
            }
        }
    }

    i_saveModifiedRegistries();

    /* fire an event */
    i_onMachineRegistered(id, FALSE);

    return hrc;
}

/**
 * Marks the registry for @a uuid as modified, so that it's saved in a later
 * call to saveModifiedRegistries().
 *
 * @param uuid
 */
void VirtualBox::i_markRegistryModified(const Guid &uuid)
{
    if (uuid == i_getGlobalRegistryId())
        ASMAtomicIncU64(&m->uRegistryNeedsSaving);
    else
    {
        ComObjPtr<Machine> pMachine;
        HRESULT hrc = i_findMachine(uuid, false /* fPermitInaccessible */, false /* aSetError */, &pMachine);
        if (SUCCEEDED(hrc))
        {
            AutoCaller machineCaller(pMachine);
            if (SUCCEEDED(machineCaller.hrc()) && pMachine->i_isAccessible())
                ASMAtomicIncU64(&pMachine->uRegistryNeedsSaving);
        }
    }
}

/**
 * Marks the registry for @a uuid as unmodified, so that it's not saved in
 * a later call to saveModifiedRegistries().
 *
 * @param uuid
 */
void VirtualBox::i_unmarkRegistryModified(const Guid &uuid)
{
    uint64_t uOld;
    if (uuid == i_getGlobalRegistryId())
    {
        for (;;)
        {
            uOld = ASMAtomicReadU64(&m->uRegistryNeedsSaving);
            if (!uOld)
                break;
            if (ASMAtomicCmpXchgU64(&m->uRegistryNeedsSaving, 0, uOld))
                break;
            ASMNopPause();
        }
    }
    else
    {
        ComObjPtr<Machine> pMachine;
        HRESULT hrc = i_findMachine(uuid, false /* fPermitInaccessible */, false /* aSetError */, &pMachine);
        if (SUCCEEDED(hrc))
        {
            AutoCaller machineCaller(pMachine);
            if (SUCCEEDED(machineCaller.hrc()))
            {
                for (;;)
                {
                    uOld = ASMAtomicReadU64(&pMachine->uRegistryNeedsSaving);
                    if (!uOld)
                        break;
                    if (ASMAtomicCmpXchgU64(&pMachine->uRegistryNeedsSaving, 0, uOld))
                        break;
                    ASMNopPause();
                }
            }
        }
    }
}

/**
 * Saves all settings files according to the modified flags in the Machine
 * objects and in the VirtualBox object.
 *
 * This locks machines and the VirtualBox object as necessary, so better not
 * hold any locks before calling this.
 */
void VirtualBox::i_saveModifiedRegistries()
{
    HRESULT hrc = S_OK;
    bool fNeedsGlobalSettings = false;
    uint64_t uOld;

    {
        AutoReadLock alock(m->allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);
        for (MachinesOList::iterator it = m->allMachines.begin();
             it != m->allMachines.end();
             ++it)
        {
            const ComObjPtr<Machine> &pMachine = *it;

            for (;;)
            {
                uOld = ASMAtomicReadU64(&pMachine->uRegistryNeedsSaving);
                if (!uOld)
                    break;
                if (ASMAtomicCmpXchgU64(&pMachine->uRegistryNeedsSaving, 0, uOld))
                    break;
                ASMNopPause();
            }
            if (uOld)
            {
                AutoCaller autoCaller(pMachine);
                if (FAILED(autoCaller.hrc()))
                    continue;
                /* object is already dead, no point in saving settings */
                if (getObjectState().getState() != ObjectState::Ready)
                    continue;
                AutoWriteLock mlock(pMachine COMMA_LOCKVAL_SRC_POS);
                hrc = pMachine->i_saveSettings(&fNeedsGlobalSettings, mlock,
                                               Machine::SaveS_Force);           // caller said save, so stop arguing
            }
        }
    }

    for (;;)
    {
        uOld = ASMAtomicReadU64(&m->uRegistryNeedsSaving);
        if (!uOld)
            break;
        if (ASMAtomicCmpXchgU64(&m->uRegistryNeedsSaving, 0, uOld))
            break;
        ASMNopPause();
    }
    if (uOld || fNeedsGlobalSettings)
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
    }
    NOREF(hrc); /* XXX */
}


/* static */
const com::Utf8Str &VirtualBox::i_getVersionNormalized()
{
    return sVersionNormalized;
}

/**
 * Checks if the path to the specified file exists, according to the path
 * information present in the file name. Optionally the path is created.
 *
 * Note that the given file name must contain the full path otherwise the
 * extracted relative path will be created based on the current working
 * directory which is normally unknown.
 *
 * @param strFileName   Full file name which path is checked/created.
 * @param fCreate       Flag if the path should be created if it doesn't exist.
 *
 * @return Extended error information on failure to check/create the path.
 */
/* static */
HRESULT VirtualBox::i_ensureFilePathExists(const Utf8Str &strFileName, bool fCreate)
{
    Utf8Str strDir(strFileName);
    strDir.stripFilename();
    if (!RTDirExists(strDir.c_str()))
    {
        if (fCreate)
        {
            int vrc = RTDirCreateFullPath(strDir.c_str(), 0700);
            if (RT_FAILURE(vrc))
                return i_setErrorStaticBoth(VBOX_E_IPRT_ERROR, vrc,
                                            tr("Could not create the directory '%s' (%Rrc)"),
                                            strDir.c_str(),
                                            vrc);
        }
        else
            return i_setErrorStaticBoth(VBOX_E_IPRT_ERROR, VERR_FILE_NOT_FOUND,
                                        tr("Directory '%s' does not exist"), strDir.c_str());
    }

    return S_OK;
}

const Utf8Str& VirtualBox::i_settingsFilePath()
{
    return m->strSettingsFilePath;
}

/**
 * Returns the lock handle which protects the machines list. As opposed
 * to version 3.1 and earlier, these lists are no longer protected by the
 * VirtualBox lock, but by this more specialized lock. Mind the locking
 * order: always request this lock after the VirtualBox object lock but
 * before the locks of any machine object. See AutoLock.h.
 */
RWLockHandle& VirtualBox::i_getMachinesListLockHandle()
{
    return m->lockMachines;
}

/**
 * Returns the lock handle which protects the media trees (hard disks,
 * DVDs, floppies). As opposed to version 3.1 and earlier, these lists
 * are no longer protected by the VirtualBox lock, but by this more
 * specialized lock. Mind the locking order: always request this lock
 * after the VirtualBox object lock but before the locks of the media
 * objects contained in these lists. See AutoLock.h.
 */
RWLockHandle& VirtualBox::i_getMediaTreeLockHandle()
{
    return m->lockMedia;
}

/**
 *  Thread function that handles custom events posted using #i_postEvent().
 */
// static
DECLCALLBACK(int) VirtualBox::AsyncEventHandler(RTTHREAD thread, void *pvUser)
{
    LogFlowFuncEnter();

    AssertReturn(pvUser, VERR_INVALID_POINTER);

    HRESULT hrc = com::Initialize();
    if (FAILED(hrc))
        return VERR_COM_UNEXPECTED;

    int vrc = VINF_SUCCESS;

    try
    {
        /* Create an event queue for the current thread. */
        EventQueue *pEventQueue = new EventQueue();
        AssertPtr(pEventQueue);

        /* Return the queue to the one who created this thread. */
        *(static_cast <EventQueue **>(pvUser)) = pEventQueue;

        /* signal that we're ready. */
        RTThreadUserSignal(thread);

        /*
         * In case of spurious wakeups causing VERR_TIMEOUTs and/or other return codes
         * we must not stop processing events and delete the pEventQueue object. This must
         * be done ONLY when we stop this loop via interruptEventQueueProcessing().
         * See @bugref{5724}.
         */
        for (;;)
        {
            vrc = pEventQueue->processEventQueue(RT_INDEFINITE_WAIT);
            if (vrc == VERR_INTERRUPTED)
            {
                LogFlow(("Event queue processing ended with vrc=%Rrc\n", vrc));
                vrc = VINF_SUCCESS; /* Set success when exiting. */
                break;
            }
        }

        delete pEventQueue;
    }
    catch (std::bad_alloc &ba)
    {
        vrc = VERR_NO_MEMORY;
        NOREF(ba);
    }

    com::Shutdown();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}


////////////////////////////////////////////////////////////////////////////////


/**
 * Called on the event handler thread.
 *
 * @note Locks the managed VirtualBox object for reading but leaves the lock
 *       before iterating over callbacks and calling their methods.
 */
void *VirtualBox::AsyncEvent::handler()
{
    if (mVirtualBox)
    {
        AutoCaller autoCaller(mVirtualBox);
        if (autoCaller.isOk())
        {
            VBoxEventDesc EvtDesc(mEvent, mVirtualBox->m->pEventSource);
            EvtDesc.fire(/* don't wait for delivery */0);
        }
        else
            Log1WarningFunc(("VirtualBox has been uninitialized (state=%d), the callback event is discarded!\n",
                             mVirtualBox->getObjectState().getState()));
        mVirtualBox = NULL; /* Old code did this, not really necessary, but whatever. */
    }
    mEvent.setNull();
    return NULL;
}

//STDMETHODIMP VirtualBox::CreateDHCPServerForInterface(/*IHostNetworkInterface * aIinterface,*/ IDHCPServer ** aServer)
//{
//    return E_NOTIMPL;
//}

HRESULT VirtualBox::createDHCPServer(const com::Utf8Str &aName,
                                     ComPtr<IDHCPServer> &aServer)
{
    ComObjPtr<DHCPServer> dhcpServer;
    dhcpServer.createObject();
    HRESULT hrc = dhcpServer->init(this, aName);
    if (FAILED(hrc)) return hrc;

    hrc = i_registerDHCPServer(dhcpServer, true);
    if (FAILED(hrc)) return hrc;

    dhcpServer.queryInterfaceTo(aServer.asOutParam());

    return hrc;
}

HRESULT VirtualBox::findDHCPServerByNetworkName(const com::Utf8Str &aName,
                                                ComPtr<IDHCPServer> &aServer)
{
    ComPtr<DHCPServer> found;

    AutoReadLock alock(m->allDHCPServers.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (DHCPServersOList::const_iterator it = m->allDHCPServers.begin();
         it != m->allDHCPServers.end();
         ++it)
    {
        Bstr bstrNetworkName;
        HRESULT hrc = (*it)->COMGETTER(NetworkName)(bstrNetworkName.asOutParam());
        if (FAILED(hrc)) return hrc;

        if (Utf8Str(bstrNetworkName) == aName)
        {
            found = *it;
            break;
        }
    }

    if (!found)
        return E_INVALIDARG;
    return found.queryInterfaceTo(aServer.asOutParam());
}

HRESULT VirtualBox::removeDHCPServer(const ComPtr<IDHCPServer> &aServer)
{
    IDHCPServer *aP = aServer;
    return i_unregisterDHCPServer(static_cast<DHCPServer *>(aP));
}

/**
 * Remembers the given DHCP server in the settings.
 *
 * @param aDHCPServer   DHCP server object to remember.
 * @param aSaveSettings @c true to save settings to disk (default).
 *
 * When @a aSaveSettings is @c true, this operation may fail because of the
 * failed #i_saveSettings() method it calls. In this case, the dhcp server object
 * will not be remembered. It is therefore the responsibility of the caller to
 * call this method as the last step of some action that requires registration
 * in order to make sure that only fully functional dhcp server objects get
 * registered.
 *
 * @note Locks this object for writing and @a aDHCPServer for reading.
 */
HRESULT VirtualBox::i_registerDHCPServer(DHCPServer *aDHCPServer,
                                         bool aSaveSettings /*= true*/)
{
    AssertReturn(aDHCPServer != NULL, E_INVALIDARG);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    // Acquire a lock on the VirtualBox object early to avoid lock order issues
    // when we call i_saveSettings() later on.
    AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
    // need it below, in findDHCPServerByNetworkName (reading) and in
    // m->allDHCPServers.addChild, so need to get it here to avoid lock
    // order trouble with dhcpServerCaller
    AutoWriteLock alock(m->allDHCPServers.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    AutoCaller dhcpServerCaller(aDHCPServer);
    AssertComRCReturnRC(dhcpServerCaller.hrc());

    Bstr bstrNetworkName;
    HRESULT hrc = aDHCPServer->COMGETTER(NetworkName)(bstrNetworkName.asOutParam());
    if (FAILED(hrc)) return hrc;

    ComPtr<IDHCPServer> existing;
    hrc = findDHCPServerByNetworkName(Utf8Str(bstrNetworkName), existing);
    if (SUCCEEDED(hrc))
        return E_INVALIDARG;
    hrc = S_OK;

    m->allDHCPServers.addChild(aDHCPServer);
    // we need to release the list lock before we attempt to acquire locks
    // on other objects in i_saveSettings (see @bugref{7500})
    alock.release();

    if (aSaveSettings)
    {
        // we acquired the lock on 'this' earlier to avoid lock order issues
        hrc = i_saveSettings();

        if (FAILED(hrc))
        {
            alock.acquire();
            m->allDHCPServers.removeChild(aDHCPServer);
        }
    }

    return hrc;
}

/**
 * Removes the given DHCP server from the settings.
 *
 * @param aDHCPServer   DHCP server object to remove.
 *
 * This operation may fail because of the failed #i_saveSettings() method it
 * calls. In this case, the DHCP server will NOT be removed from the settings
 * when this method returns.
 *
 * @note Locks this object for writing.
 */
HRESULT VirtualBox::i_unregisterDHCPServer(DHCPServer *aDHCPServer)
{
    AssertReturn(aDHCPServer != NULL, E_INVALIDARG);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoCaller dhcpServerCaller(aDHCPServer);
    AssertComRCReturnRC(dhcpServerCaller.hrc());

    AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock alock(m->allDHCPServers.getLockHandle() COMMA_LOCKVAL_SRC_POS);
    m->allDHCPServers.removeChild(aDHCPServer);
    // we need to release the list lock before we attempt to acquire locks
    // on other objects in i_saveSettings (see @bugref{7500})
    alock.release();

    HRESULT hrc = i_saveSettings();

    // undo the changes if we failed to save them
    if (FAILED(hrc))
    {
        alock.acquire();
        m->allDHCPServers.addChild(aDHCPServer);
    }

    return hrc;
}


/**
 * NAT Network
 */
HRESULT VirtualBox::createNATNetwork(const com::Utf8Str &aNetworkName,
                                     ComPtr<INATNetwork> &aNetwork)
{
#ifdef VBOX_WITH_NAT_SERVICE
    ComObjPtr<NATNetwork> natNetwork;
    natNetwork.createObject();
    HRESULT hrc = natNetwork->init(this, aNetworkName);
    if (FAILED(hrc)) return hrc;

    hrc = i_registerNATNetwork(natNetwork, true);
    if (FAILED(hrc)) return hrc;

    natNetwork.queryInterfaceTo(aNetwork.asOutParam());

    ::FireNATNetworkCreationDeletionEvent(m->pEventSource, aNetworkName, TRUE);

    return hrc;
#else
    NOREF(aNetworkName);
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif
}

HRESULT VirtualBox::findNATNetworkByName(const com::Utf8Str &aNetworkName,
                                         ComPtr<INATNetwork> &aNetwork)
{
#ifdef VBOX_WITH_NAT_SERVICE

    HRESULT hrc = S_OK;
    ComPtr<NATNetwork> found;

    AutoReadLock alock(m->allNATNetworks.getLockHandle() COMMA_LOCKVAL_SRC_POS);

    for (NATNetworksOList::const_iterator it = m->allNATNetworks.begin();
         it != m->allNATNetworks.end();
         ++it)
    {
        Bstr bstrNATNetworkName;
        hrc = (*it)->COMGETTER(NetworkName)(bstrNATNetworkName.asOutParam());
        if (FAILED(hrc)) return hrc;

        if (Utf8Str(bstrNATNetworkName) == aNetworkName)
        {
            found = *it;
            break;
        }
    }

    if (!found)
        return E_INVALIDARG;
    found.queryInterfaceTo(aNetwork.asOutParam());
    return hrc;
#else
    NOREF(aNetworkName);
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif
}

HRESULT VirtualBox::removeNATNetwork(const ComPtr<INATNetwork> &aNetwork)
{
#ifdef VBOX_WITH_NAT_SERVICE
    Bstr name;
    HRESULT hrc = aNetwork->COMGETTER(NetworkName)(name.asOutParam());
    if (FAILED(hrc))
        return hrc;
    INATNetwork *p = aNetwork;
    NATNetwork *network = static_cast<NATNetwork *>(p);
    hrc = i_unregisterNATNetwork(network, true);
    ::FireNATNetworkCreationDeletionEvent(m->pEventSource, name.raw(), FALSE);
    return hrc;
#else
    NOREF(aNetwork);
    return E_NOTIMPL;
#endif

}
/**
 * Remembers the given NAT network in the settings.
 *
 * @param aNATNetwork    NAT Network object to remember.
 * @param aSaveSettings @c true to save settings to disk (default).
 *
 *
 * @note Locks this object for writing and @a aNATNetwork for reading.
 */
HRESULT VirtualBox::i_registerNATNetwork(NATNetwork *aNATNetwork,
                                         bool aSaveSettings /*= true*/)
{
#ifdef VBOX_WITH_NAT_SERVICE
    AssertReturn(aNATNetwork != NULL, E_INVALIDARG);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoCaller natNetworkCaller(aNATNetwork);
    AssertComRCReturnRC(natNetworkCaller.hrc());

    Bstr name;
    HRESULT hrc;
    hrc = aNATNetwork->COMGETTER(NetworkName)(name.asOutParam());
    AssertComRCReturnRC(hrc);

    /* returned value isn't 0 and aSaveSettings is true
     * means that we create duplicate, otherwise we just load settings.
     */
    if (   sNatNetworkNameToRefCount[name]
        && aSaveSettings)
        AssertComRCReturnRC(E_INVALIDARG);

    hrc = S_OK;

    sNatNetworkNameToRefCount[name] = 0;

    m->allNATNetworks.addChild(aNATNetwork);

    if (aSaveSettings)
    {
        AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
        vboxLock.release();

        if (FAILED(hrc))
            i_unregisterNATNetwork(aNATNetwork, false /* aSaveSettings */);
    }

    return hrc;
#else
    NOREF(aNATNetwork);
    NOREF(aSaveSettings);
    /* No panic please (silently ignore) */
    return S_OK;
#endif
}

/**
 * Removes the given NAT network from the settings.
 *
 * @param aNATNetwork   NAT network object to remove.
 * @param aSaveSettings @c true to save settings to disk (default).
 *
 * When @a aSaveSettings is @c true, this operation may fail because of the
 * failed #i_saveSettings() method it calls. In this case, the DHCP server
 * will NOT be removed from the settingsi when this method returns.
 *
 * @note Locks this object for writing.
 */
HRESULT VirtualBox::i_unregisterNATNetwork(NATNetwork *aNATNetwork,
                                           bool aSaveSettings /*= true*/)
{
#ifdef VBOX_WITH_NAT_SERVICE
    AssertReturn(aNATNetwork != NULL, E_INVALIDARG);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoCaller natNetworkCaller(aNATNetwork);
    AssertComRCReturnRC(natNetworkCaller.hrc());

    Bstr name;
    HRESULT hrc = aNATNetwork->COMGETTER(NetworkName)(name.asOutParam());
    /* Hm, there're still running clients. */
    if (FAILED(hrc) || sNatNetworkNameToRefCount[name])
        AssertComRCReturnRC(E_INVALIDARG);

    m->allNATNetworks.removeChild(aNATNetwork);

    if (aSaveSettings)
    {
        AutoWriteLock vboxLock(this COMMA_LOCKVAL_SRC_POS);
        hrc = i_saveSettings();
        vboxLock.release();

        if (FAILED(hrc))
            i_registerNATNetwork(aNATNetwork, false /* aSaveSettings */);
    }

    return hrc;
#else
    NOREF(aNATNetwork);
    NOREF(aSaveSettings);
    return E_NOTIMPL;
#endif
}

HRESULT VirtualBox::findProgressById(const com::Guid &aId,
                                     ComPtr<IProgress> &aProgressObject)
{
    if (!aId.isValid())
        return setError(E_INVALIDARG, tr("The provided progress object GUID is invalid"));

#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER //0 /** @todo def VBOX_WITH_MAIN_OBJECT_TRACKER - never used */
    std::vector<com::Utf8Str> lObjIdMap;
    gTrackedObjectsCollector.getObjIdsByClassIID(IID_IProgress, lObjIdMap);

    for (const com::Utf8Str &item : lObjIdMap)
    {
        if (gTrackedObjectsCollector.checkObj(item.c_str()))
        {
            TrackedObjectData temp;
            gTrackedObjectsCollector.getObj(item.c_str(), temp);
            Log2(("Tracked Progress Object with objectId %s was found\n", temp.objectIdStr().c_str()));

            ComPtr<IProgress> pProgress;
            temp.getInterface()->QueryInterface(IID_IProgress, (void **)pProgress.asOutParam());
            if (pProgress.isNotNull())
            {
                com::Bstr reqId(aId.toString());
                Bstr foundId;
                pProgress->COMGETTER(Id)(foundId.asOutParam());
                if (reqId == foundId)
                {
                    BOOL aCompleted;
                    pProgress->COMGETTER(Completed)(&aCompleted);

                    BOOL aCanceled;
                    pProgress->COMGETTER(Canceled)(&aCanceled);
                    Log2(("Requested progress was found:\n  id %s\n  completed %s\n  canceled %s\n",
                            aId.toString().c_str(),
                            aCompleted ? "True" : "False",
                            aCanceled ? "True" : "False"));

                    aProgressObject = pProgress;
                    return S_OK;
                }
            }
        }
    }

#else /* !VBOX_WITH_MAIN_OBJECT_TRACKER */
    /* protect mProgressOperations */
    AutoReadLock safeLock(m->mtxProgressOperations COMMA_LOCKVAL_SRC_POS);

    ProgressMap::const_iterator it = m->mapProgressOperations.find(aId);
    if (it != m->mapProgressOperations.end())
    {
        aProgressObject = it->second;
        return S_OK;
    }
#endif /* !VBOX_WITH_MAIN_OBJECT_TRACKER */

    /** @todo r=bird: E_INVALIDARG isn't a good choice here... */
    return setError(E_INVALIDARG, tr("The progress object with the given GUID could not be found"));
}

/**
 * Get the tracked object by the Id.
 *
 * @param aTrObjId  tracked object Id
 * @param aPIface  returns the ComPtr<IUnknown> if the object is found
 * @param aState  returns TrackedObjectState_T - the actual state of the found
 *                object
 * @param aCreationTime  returns the creation time of the object
 * @param aDeletionTime  returns the deletion time of the object
 *
 * @return HRESULT
 */
HRESULT VirtualBox::getTrackedObject(const com::Utf8Str &aTrObjId,
                                     ComPtr<IUnknown> &aPIface,
                                     TrackedObjectState_T *aState,
                                     LONG64 *aCreationTime,
                                     LONG64 *aDeletionTime)
{
#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
    TrackedObjectData trObjData;
    HRESULT hrc = gTrackedObjectsCollector.getObj(aTrObjId, trObjData);
    if (SUCCEEDED(hrc))
    {
        trObjData.getInterface().queryInterfaceTo(aPIface.asOutParam());
        RTTIMESPEC time = trObjData.creationTime();
        *aCreationTime = RTTimeSpecGetMilli(&time);
        *aState = trObjData.state();
        if (*aState != TrackedObjectState_Alive)
        {
            time = trObjData.deletionTime();
            *aDeletionTime = RTTimeSpecGetMilli(&time);
        }
        else
            /* aDeletionTime is set to 0 */
            *aDeletionTime = 0;
    }

    return hrc;

#else
    RT_NOREF(aTrObjId, aPIface, aState, aCreationTime, aDeletionTime);
    return E_NOTIMPL;
#endif
}


#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
static std::map<com::Utf8Str, com::Utf8Str> const g_lMapInterfaceNameToIID = {
    {"IProgress", Guid(IID_IProgress).toString()},
};
#endif

/**
 * Get the tracked object Ids list by the interface name.
 *
 * @param aName  Interface name (like "IProgress", "ISession", "IMedium" etc)
 * @param aObjIdsList  The list of Ids of the found objects
 *
 * @return The list of the found objects Ids
 */
HRESULT VirtualBox::getTrackedObjectIds(const com::Utf8Str &aName,
                                        std::vector<com::Utf8Str> &aObjIdsList)
{
#ifdef VBOX_WITH_MAIN_OBJECT_TRACKER
    HRESULT hrc = S_OK;

    try
    {
        /* Check the supported tracked classes to avoid "out of range" exception */
        if (g_lMapInterfaceNameToIID.count(aName))
        {
            hrc = gTrackedObjectsCollector.getObjIdsByClassIID(g_lMapInterfaceNameToIID.at(aName), aObjIdsList);
            if (FAILED(hrc))
                hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("No objects were found for the passed interface name '%s'."),
                               aName.c_str());
        }
        else
            hrc = setError(E_INVALIDARG, tr("The objects of the passed interface '%s' are not tracked at moment."), aName.c_str());
    }
    catch (...)
    {
            hrc = setError(E_FAIL, tr("The unknown exception in the VirtualBox::getTrackedObjectIds()."));
    }

    return hrc;

#else
    RT_NOREF(aName, aObjIdsList);
    return E_NOTIMPL;
#endif
}

/**
 * Retains a reference to the default cryptographic interface.
 *
 * @returns COM status code.
 * @param   ppCryptoIf          Where to store the pointer to the cryptographic interface on success.
 *
 * @note Locks this object for writing.
 */
HRESULT VirtualBox::i_retainCryptoIf(PCVBOXCRYPTOIF *ppCryptoIf)
{
    AssertReturn(ppCryptoIf != NULL, E_INVALIDARG);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /*
     * No object lock due to some lock order fun with Machine objects.
     * There is a dedicated critical section to protect against concurrency
     * issues when loading the module.
     */
    RTCritSectEnter(&m->CritSectModCrypto);

    /* Try to load the extension pack module if it isn't currently. */
    HRESULT hrc = S_OK;
    if (m->hLdrModCrypto == NIL_RTLDRMOD)
    {
#ifdef VBOX_WITH_EXTPACK
        /*
         * Check that a crypto extension pack name is set and resolve it into a
         * library path.
         */
        Utf8Str strExtPack;
        hrc = m->pSystemProperties->getDefaultCryptoExtPack(strExtPack);
        if (FAILED(hrc))
        {
            RTCritSectLeave(&m->CritSectModCrypto);
            return hrc;
        }
        if (strExtPack.isEmpty())
        {
            RTCritSectLeave(&m->CritSectModCrypto);
            return setError(VBOX_E_OBJECT_NOT_FOUND,
                            tr("Ńo extension pack providing a cryptographic support module could be found"));
        }

        Utf8Str strCryptoLibrary;
        int vrc = m->ptrExtPackManager->i_getCryptoLibraryPathForExtPack(&strExtPack, &strCryptoLibrary);
        if (RT_SUCCESS(vrc))
        {
            RTERRINFOSTATIC ErrInfo;
            vrc = SUPR3HardenedLdrLoadPlugIn(strCryptoLibrary.c_str(), &m->hLdrModCrypto, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(vrc))
            {
                /* Resolve the entry point and query the pointer to the cryptographic interface. */
                PFNVBOXCRYPTOENTRY pfnCryptoEntry = NULL;
                vrc = RTLdrGetSymbol(m->hLdrModCrypto, VBOX_CRYPTO_MOD_ENTRY_POINT, (void **)&pfnCryptoEntry);
                if (RT_SUCCESS(vrc))
                {
                    vrc = pfnCryptoEntry(&m->pCryptoIf);
                    if (RT_FAILURE(vrc))
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                           tr("Failed to query the interface callback table from the cryptographic support module '%s' from extension pack '%s'"),
                                           strCryptoLibrary.c_str(), strExtPack.c_str());
                }
                else
                    hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                       tr("Failed to resolve the entry point for the cryptographic support module '%s' from extension pack '%s'"),
                                       strCryptoLibrary.c_str(), strExtPack.c_str());
            }
            else
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                   tr("Couldn't load the cryptographic support module '%s' from extension pack '%s' (error: '%s')"),
                                   strCryptoLibrary.c_str(), strExtPack.c_str(), ErrInfo.Core.pszMsg);
        }
        else
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                               tr("Couldn't resolve the library path of the crpytographic support module for extension pack '%s'"),
                               strExtPack.c_str());
#else
        hrc = setError(VBOX_E_NOT_SUPPORTED,
                       tr("The cryptographic support module is not supported in this build because extension packs are not supported"));
#endif
    }

    if (SUCCEEDED(hrc))
    {
        ASMAtomicIncU32(&m->cRefsCrypto);
        *ppCryptoIf = m->pCryptoIf;
    }

    RTCritSectLeave(&m->CritSectModCrypto);

    return hrc;
}


/**
 * Releases the reference of the given cryptographic interface.
 *
 * @returns COM status code.
 * @param   pCryptoIf           Pointer to the cryptographic interface to release.
 *
 * @note Locks this object for writing.
 */
HRESULT VirtualBox::i_releaseCryptoIf(PCVBOXCRYPTOIF pCryptoIf)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AssertReturn(pCryptoIf == m->pCryptoIf, E_INVALIDARG);

    ASMAtomicDecU32(&m->cRefsCrypto);
    return S_OK;
}


/**
 * Tries to unload any loaded cryptographic support module if it is not in use currently.
 *
 * @returns COM status code.
 *
 * @note Locks this object for writing.
 */
HRESULT VirtualBox::i_unloadCryptoIfModule(void)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    if (m->cRefsCrypto)
        return setError(E_ACCESSDENIED,
                        tr("The cryptographic support module is in use and can't be unloaded"));

    RTCritSectEnter(&m->CritSectModCrypto);
    if (m->hLdrModCrypto != NIL_RTLDRMOD)
    {
        int vrc = RTLdrClose(m->hLdrModCrypto);
        AssertRC(vrc);
        m->hLdrModCrypto = NIL_RTLDRMOD;
    }
    RTCritSectLeave(&m->CritSectModCrypto);

    return S_OK;
}


#ifdef RT_OS_WINDOWS
#include <psapi.h>

/**
 * Report versions of installed drivers to release log.
 */
void VirtualBox::i_reportDriverVersions()
{
    /** @todo r=klaus this code is very confusing, as it uses TCHAR (and
     * randomly also _TCHAR, which sounds to me like asking for trouble),
     * the "sz" variable prefix but "%ls" for the format string - so the whole
     * thing is better compiled with UNICODE and _UNICODE defined. Would be
     * far easier to read if it would be coded explicitly for the unicode
     * case, as it won't work otherwise. */
    DWORD   err;
    HRESULT hrc;
    LPVOID  aDrivers[1024];
    LPVOID *pDrivers      = aDrivers;
    UINT    cNeeded       = 0;
    TCHAR   szSystemRoot[MAX_PATH];
    TCHAR  *pszSystemRoot = szSystemRoot;
    LPVOID  pVerInfo      = NULL;
    DWORD   cbVerInfo     = 0;

    do
    {
        cNeeded = GetWindowsDirectory(szSystemRoot, RT_ELEMENTS(szSystemRoot));
        if (cNeeded == 0)
        {
            err = GetLastError();
            hrc = HRESULT_FROM_WIN32(err);
            AssertLogRelMsgFailed(("GetWindowsDirectory failed, hrc=%Rhrc (0x%x) err=%u\n",
                                                   hrc, hrc, err));
            break;
        }
        else if (cNeeded > RT_ELEMENTS(szSystemRoot))
        {
            /* The buffer is too small, allocate big one. */
            pszSystemRoot = (TCHAR *)RTMemTmpAlloc(cNeeded * sizeof(_TCHAR));
            if (!pszSystemRoot)
            {
                AssertLogRelMsgFailed(("RTMemTmpAlloc failed to allocate %d bytes\n", cNeeded));
                break;
            }
            if (GetWindowsDirectory(pszSystemRoot, cNeeded) == 0)
            {
                err = GetLastError();
                hrc = HRESULT_FROM_WIN32(err);
                AssertLogRelMsgFailed(("GetWindowsDirectory failed, hrc=%Rhrc (0x%x) err=%u\n",
                                                   hrc, hrc, err));
                break;
            }
        }

        DWORD  cbNeeded = 0;
        if (!EnumDeviceDrivers(aDrivers, sizeof(aDrivers), &cbNeeded) || cbNeeded > sizeof(aDrivers))
        {
            pDrivers = (LPVOID *)RTMemTmpAlloc(cbNeeded);
            if (!EnumDeviceDrivers(pDrivers, cbNeeded, &cbNeeded))
            {
                err = GetLastError();
                hrc = HRESULT_FROM_WIN32(err);
                AssertLogRelMsgFailed(("EnumDeviceDrivers failed, hrc=%Rhrc (0x%x) err=%u\n",
                                                   hrc, hrc, err));
                break;
            }
        }

        LogRel(("Installed Drivers:\n"));

        TCHAR szDriver[1024];
        int cDrivers = cbNeeded / sizeof(pDrivers[0]);
        for (int i = 0; i < cDrivers; i++)
        {
            if (GetDeviceDriverBaseName(pDrivers[i], szDriver, sizeof(szDriver) / sizeof(szDriver[0])))
            {
                if (_tcsnicmp(TEXT("vbox"), szDriver, 4))
                    continue;
            }
            else
                continue;
            if (GetDeviceDriverFileName(pDrivers[i], szDriver, sizeof(szDriver) / sizeof(szDriver[0])))
            {
                _TCHAR szTmpDrv[1024];
                _TCHAR *pszDrv = szDriver;
                if (!_tcsncmp(TEXT("\\SystemRoot"), szDriver, 11))
                {
                    _tcscpy_s(szTmpDrv, pszSystemRoot);
                    _tcsncat_s(szTmpDrv, szDriver + 11, sizeof(szTmpDrv) / sizeof(szTmpDrv[0]) - _tclen(pszSystemRoot));
                    pszDrv = szTmpDrv;
                }
                else if (!_tcsncmp(TEXT("\\??\\"), szDriver, 4))
                    pszDrv = szDriver + 4;

                /* Allocate a buffer for version info. Reuse if large enough. */
                DWORD cbNewVerInfo = GetFileVersionInfoSize(pszDrv, NULL);
                if (cbNewVerInfo > cbVerInfo)
                {
                    if (pVerInfo)
                        RTMemTmpFree(pVerInfo);
                    cbVerInfo = cbNewVerInfo;
                    pVerInfo = RTMemTmpAlloc(cbVerInfo);
                    if (!pVerInfo)
                    {
                        AssertLogRelMsgFailed(("RTMemTmpAlloc failed to allocate %d bytes\n", cbVerInfo));
                        break;
                    }
                }

                if (GetFileVersionInfo(pszDrv, NULL, cbVerInfo, pVerInfo))
                {
                    UINT   cbSize = 0;
                    LPBYTE lpBuffer = NULL;
                    if (VerQueryValue(pVerInfo, TEXT("\\"), (VOID FAR* FAR*)&lpBuffer, &cbSize))
                    {
                        if (cbSize)
                        {
                            VS_FIXEDFILEINFO *pFileInfo = (VS_FIXEDFILEINFO *)lpBuffer;
                            if (pFileInfo->dwSignature == 0xfeef04bd)
                            {
                                LogRel(("  %ls (Version: %d.%d.%d.%d)\n", pszDrv,
                                        (pFileInfo->dwFileVersionMS >> 16) & 0xffff,
                                        (pFileInfo->dwFileVersionMS >> 0) & 0xffff,
                                        (pFileInfo->dwFileVersionLS >> 16) & 0xffff,
                                        (pFileInfo->dwFileVersionLS >> 0) & 0xffff));
                            }
                        }
                    }
                }
            }
        }

    }
    while (0);

    if (pVerInfo)
        RTMemTmpFree(pVerInfo);

    if (pDrivers != aDrivers)
        RTMemTmpFree(pDrivers);

    if (pszSystemRoot != szSystemRoot)
        RTMemTmpFree(pszSystemRoot);
}
#else /* !RT_OS_WINDOWS */
void VirtualBox::i_reportDriverVersions(void)
{
}
#endif /* !RT_OS_WINDOWS */

#if defined(RT_OS_WINDOWS) && defined(VBOXSVC_WITH_CLIENT_WATCHER)

# include <psapi.h> /* for GetProcessImageFileNameW */

/**
 * Callout from the wrapper.
 */
void VirtualBox::i_callHook(const char *a_pszFunction)
{
    RT_NOREF(a_pszFunction);

    /*
     * Let'see figure out who is calling.
     * Note! Requires Vista+, so skip this entirely on older systems.
     */
    if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
    {
        RPC_CALL_ATTRIBUTES_V2_W CallAttribs = { RPC_CALL_ATTRIBUTES_VERSION, RPC_QUERY_CLIENT_PID | RPC_QUERY_IS_CLIENT_LOCAL };
        RPC_STATUS rcRpc = RpcServerInqCallAttributesW(NULL, &CallAttribs);
        if (   rcRpc == RPC_S_OK
            && CallAttribs.ClientPID != 0)
        {
            RTPROCESS const pidClient = (RTPROCESS)(uintptr_t)CallAttribs.ClientPID;
            if (pidClient != RTProcSelf())
            {
                /** @todo LogRel2 later: */
                LogRel(("i_callHook: %Rfn [ClientPID=%#zx/%zu IsClientLocal=%d ProtocolSequence=%#x CallStatus=%#x CallType=%#x OpNum=%#x InterfaceUuid=%RTuuid]\n",
                        a_pszFunction, CallAttribs.ClientPID, CallAttribs.ClientPID, CallAttribs.IsClientLocal,
                        CallAttribs.ProtocolSequence, CallAttribs.CallStatus, CallAttribs.CallType, CallAttribs.OpNum,
                        &CallAttribs.InterfaceUuid));

                /*
                 * Do we know this client PID already?
                 */
                RTCritSectRwEnterShared(&m->WatcherCritSect);
                WatchedClientProcessMap::iterator It = m->WatchedProcesses.find(pidClient);
                if (It != m->WatchedProcesses.end())
                    RTCritSectRwLeaveShared(&m->WatcherCritSect); /* Known process, nothing to do. */
                else
                {
                    /* This is a new client process, start watching it. */
                    RTCritSectRwLeaveShared(&m->WatcherCritSect);
                    i_watchClientProcess(pidClient, a_pszFunction);
                }
            }
        }
        else
            LogRel(("i_callHook: %Rfn - rcRpc=%#x ClientPID=%#zx/%zu !! [IsClientLocal=%d ProtocolSequence=%#x CallStatus=%#x CallType=%#x OpNum=%#x InterfaceUuid=%RTuuid]\n",
                    a_pszFunction, rcRpc, CallAttribs.ClientPID, CallAttribs.ClientPID, CallAttribs.IsClientLocal,
                    CallAttribs.ProtocolSequence, CallAttribs.CallStatus, CallAttribs.CallType, CallAttribs.OpNum,
                    &CallAttribs.InterfaceUuid));
    }
}


/**
 * Watches @a a_pidClient for termination.
 *
 * @returns true if successfully enabled watching of it, false if not.
 * @param   a_pidClient     The PID to watch.
 * @param   a_pszFunction   The function we which we detected the client in.
 */
bool VirtualBox::i_watchClientProcess(RTPROCESS a_pidClient, const char *a_pszFunction)
{
    RT_NOREF_PV(a_pszFunction);

    /*
     * Open the client process.
     */
    HANDLE hClient = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE /*fInherit*/, a_pidClient);
    if (hClient == NULL)
        hClient = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE , a_pidClient);
    if (hClient == NULL)
        hClient = OpenProcess(SYNCHRONIZE, FALSE , a_pidClient);
    AssertLogRelMsgReturn(hClient != NULL, ("pidClient=%d (%#x) err=%d\n", a_pidClient, a_pidClient, GetLastError()),
                          m->fWatcherIsReliable = false);

    /*
     * Create a new watcher structure and try add it to the map.
     */
    bool fRet = true;
    WatchedClientProcess *pWatched = new (std::nothrow) WatchedClientProcess(a_pidClient, hClient);
    if (pWatched)
    {
        RTCritSectRwEnterExcl(&m->WatcherCritSect);

        WatchedClientProcessMap::iterator It = m->WatchedProcesses.find(a_pidClient);
        if (It == m->WatchedProcesses.end())
        {
            try
            {
                m->WatchedProcesses.insert(WatchedClientProcessMap::value_type(a_pidClient, pWatched));
            }
            catch (std::bad_alloc &)
            {
                fRet = false;
            }
            if (fRet)
            {
                /*
                 * Schedule it on a watcher thread.
                 */
                /** @todo later. */
                RTCritSectRwLeaveExcl(&m->WatcherCritSect);
            }
            else
            {
                RTCritSectRwLeaveExcl(&m->WatcherCritSect);
                delete pWatched;
                LogRel(("VirtualBox::i_watchClientProcess: out of memory inserting into client map!\n"));
            }
        }
        else
        {
            /*
             * Someone raced us here, we lost.
             */
            RTCritSectRwLeaveExcl(&m->WatcherCritSect);
            delete pWatched;
        }
    }
    else
    {
        LogRel(("VirtualBox::i_watchClientProcess: out of memory!\n"));
        CloseHandle(hClient);
        m->fWatcherIsReliable = fRet = false;
    }
    return fRet;
}


/** Logs the RPC caller info to the release log. */
/*static*/ void VirtualBox::i_logCaller(const char *a_pszFormat, ...)
{
    if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
    {
        char szTmp[80];
        va_list va;
        va_start(va, a_pszFormat);
        RTStrPrintfV(szTmp, sizeof(szTmp), a_pszFormat, va);
        va_end(va);

        RPC_CALL_ATTRIBUTES_V2_W CallAttribs = { RPC_CALL_ATTRIBUTES_VERSION, RPC_QUERY_CLIENT_PID | RPC_QUERY_IS_CLIENT_LOCAL };
        RPC_STATUS rcRpc = RpcServerInqCallAttributesW(NULL, &CallAttribs);

        RTUTF16 wszProcName[256];
        wszProcName[0] = '\0';
        if (rcRpc == 0 && CallAttribs.ClientPID != 0)
        {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)(uintptr_t)CallAttribs.ClientPID);
            if (hProcess)
            {
                RT_ZERO(wszProcName);
                GetProcessImageFileNameW(hProcess, wszProcName, RT_ELEMENTS(wszProcName) - 1);
                CloseHandle(hProcess);
            }
        }
        LogRel(("%s [rcRpc=%#x ClientPID=%#zx/%zu (%ls) IsClientLocal=%d ProtocolSequence=%#x CallStatus=%#x CallType=%#x OpNum=%#x InterfaceUuid=%RTuuid]\n",
                szTmp, rcRpc, CallAttribs.ClientPID, CallAttribs.ClientPID, wszProcName, CallAttribs.IsClientLocal,
                CallAttribs.ProtocolSequence, CallAttribs.CallStatus, CallAttribs.CallType, CallAttribs.OpNum,
                &CallAttribs.InterfaceUuid));
    }
}

#endif /* RT_OS_WINDOWS && VBOXSVC_WITH_CLIENT_WATCHER */


/* vi: set tabstop=4 shiftwidth=4 expandtab: */
