/* $Id: VBoxWatchdog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxWatchdog.cpp - VirtualBox Watchdog.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#ifndef VBOX_ONLY_DOCS
# include <VBox/com/com.h>
# include <VBox/com/string.h>
# include <VBox/com/Guid.h>
# include <VBox/com/array.h>
# include <VBox/com/ErrorInfo.h>
# include <VBox/com/errorprint.h>

# include <VBox/com/NativeEventQueue.h>
# include <VBox/com/listeners.h>
# include <VBox/com/VirtualBox.h>
#endif /* !VBOX_ONLY_DOCS */

#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include <package-generated.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/critsect.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/time.h>

#include <algorithm>
#include <signal.h>

#include "VBoxWatchdogInternal.h"

using namespace com;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The details of the services that has been compiled in.
 */
typedef struct VBOXWATCHDOGMOD
{
    /** Pointer to the service descriptor. */
    PCVBOXMODULE    pDesc;
    /** Whether Pre-init was called. */
    bool            fPreInited;
    /** Whether the module is enabled or not. */
    bool            fEnabled;
} VBOXWATCHDOGMOD, *PVBOXWATCHDOGMOD;

enum GETOPTDEF_WATCHDOG
{
    GETOPTDEF_WATCHDOG_DISABLE_MODULE = 1000,
    GETOPTDEF_WATCHDOG_DRYRUN
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** External globals. */
bool                                g_fDryrun     = false;
bool                                g_fVerbose    = false;
ComPtr<IVirtualBox>                 g_pVirtualBox = NULL;
ComPtr<ISession>                    g_pSession    = NULL;
mapVM                               g_mapVM;
mapGroup                            g_mapGroup;
#ifdef VBOX_WATCHDOG_GLOBAL_PERFCOL
ComPtr<IPerformanceCollector>       g_pPerfCollector = NULL;
#endif

/** The critical section for the machines map. */
static RTCRITSECT    g_csMachines;

/** Set by the signal handler. */
static volatile bool g_fCanceled = false;

/** Logging parameters. */
static uint32_t      g_cHistory = 10;                   /* Enable log rotation, 10 files. */
static uint32_t      g_uHistoryFileTime = RT_SEC_1DAY;  /* Max 1 day per file. */
static uint64_t      g_uHistoryFileSize = 100 * _1M;    /* Max 100MB per file. */

/** Run in background. */
static bool          g_fDaemonize = false;

static VBOXWATCHDOGMOD g_aModules[] =
{
    { &g_ModBallooning, false /* Pre-inited */, true /* Enabled */ },
    { &g_ModAPIMonitor, false /* Pre-inited */, true /* Enabled */ }
};

/**
 * Command line arguments.
 */
static const RTGETOPTDEF g_aOptions[] =
{
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
    { "--background",           'b',                                       RTGETOPT_REQ_NOTHING },
#endif
    /** For displayHelp(). */
    { "--disable-<module>",     GETOPTDEF_WATCHDOG_DISABLE_MODULE, RTGETOPT_REQ_NOTHING },
    { "--dryrun",               GETOPTDEF_WATCHDOG_DRYRUN,         RTGETOPT_REQ_NOTHING },
    { "--help",                 'h',                               RTGETOPT_REQ_NOTHING },
    { "--verbose",              'v',                               RTGETOPT_REQ_NOTHING },
    { "--pidfile",              'P',                               RTGETOPT_REQ_STRING },
    { "--logfile",              'F',                               RTGETOPT_REQ_STRING },
    { "--logrotate",            'R',                               RTGETOPT_REQ_UINT32 },
    { "--logsize",              'S',                               RTGETOPT_REQ_UINT64 },
    { "--loginterval",          'I',                               RTGETOPT_REQ_UINT32 }
};

/** Global static objects. */
static ComPtr<IVirtualBoxClient> g_pVirtualBoxClient = NULL;
static ComPtr<IEventSource>      g_pEventSource = NULL;
static ComPtr<IEventSource>      g_pEventSourceClient = NULL;
static ComPtr<IEventListener>    g_pVBoxEventListener = NULL;
static NativeEventQueue         *g_pEventQ = NULL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int machineAdd(const Bstr &strUuid);
static int machineRemove(const Bstr &strUuid);
static int watchdogSetup();
static void watchdogShutdown();


/**
 *  Handler for global events.
 */
class VirtualBoxEventListener
{
    public:
        VirtualBoxEventListener()
        {
        }

        virtual ~VirtualBoxEventListener()
        {
        }

        HRESULT init()
        {
            return S_OK;
        }

        void uninit()
        {
        }

        STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
        {
            switch (aType)
            {
                case VBoxEventType_OnMachineRegistered:
                {
                    ComPtr<IMachineRegisteredEvent> pEvent = aEvent;
                    Assert(pEvent);

                    Bstr uuid;
                    BOOL fRegistered;
                    HRESULT hrc = pEvent->COMGETTER(Registered)(&fRegistered);
                    if (SUCCEEDED(hrc))
                        hrc = pEvent->COMGETTER(MachineId)(uuid.asOutParam());

                    if (SUCCEEDED(hrc))
                    {
                        int rc = RTCritSectEnter(&g_csMachines);
                        if (RT_SUCCESS(rc))
                        {
                            rc = fRegistered
                               ? machineAdd(uuid)
                               : machineRemove(uuid);
                            int rc2 = RTCritSectLeave(&g_csMachines);
                            if (RT_SUCCESS(rc))
                                rc = rc2;
                            AssertRC(rc);
                        }
                    }
                    break;
                }

                case VBoxEventType_OnMachineStateChanged:
                {
                    ComPtr<IMachineStateChangedEvent> pEvent = aEvent;
                    Assert(pEvent);

                    MachineState_T machineState;
                    Bstr uuid;

                    HRESULT hrc = pEvent->COMGETTER(State)(&machineState);
                    if (SUCCEEDED(hrc))
                        hrc = pEvent->COMGETTER(MachineId)(uuid.asOutParam());

                    if (SUCCEEDED(hrc))
                    {
                        int rc = RTCritSectEnter(&g_csMachines);
                        if (RT_SUCCESS(rc))
                        {
                            for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
                                if (g_aModules[j].fEnabled)
                                {
                                    int rc2 = g_aModules[j].pDesc->pfnOnMachineStateChanged(uuid,
                                                                                            machineState);
                                    if (RT_FAILURE(rc2))
                                        serviceLog("Module '%s' reported an error: %Rrc\n",
                                                   g_aModules[j].pDesc->pszName, rc);
                                    /* Keep going. */
                                }

                            int rc2 = RTCritSectLeave(&g_csMachines);
                            if (RT_SUCCESS(rc))
                                rc = rc2;
                            AssertRC(rc);
                        }
                    }
                    break;
                }

                case VBoxEventType_OnVBoxSVCAvailabilityChanged:
                {
                    ComPtr<IVBoxSVCAvailabilityChangedEvent> pVSACEv = aEvent;
                    Assert(pVSACEv);
                    BOOL fAvailable = FALSE;
                    pVSACEv->COMGETTER(Available)(&fAvailable);

                    /* First, notify all modules. */
                    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
                        if (g_aModules[j].fEnabled)
                        {
                            int rc2 = g_aModules[j].pDesc->pfnOnServiceStateChanged(RT_BOOL(fAvailable));
                            if (RT_FAILURE(rc2))
                                serviceLog("Module '%s' reported an error: %Rrc\n",
                                           g_aModules[j].pDesc->pszName, rc2);
                            /* Keep going. */
                        }

                    /* Do global teardown/re-creation stuff. */
                    if (!fAvailable)
                    {
                        serviceLog("VBoxSVC became unavailable\n");
                        watchdogShutdown();
                    }
                    else
                    {
                        serviceLog("VBoxSVC became available\n");
                        int rc2 = watchdogSetup();
                        if (RT_FAILURE(rc2))
                            serviceLog("Unable to re-set up watchdog (rc=%Rrc)!\n", rc2);
                    }

                    break;
                }

                default:
                    /* Not handled event, just skip it. */
                    break;
            }

            return S_OK;
        }

    private:
};
typedef ListenerImpl<VirtualBoxEventListener> VirtualBoxEventListenerImpl;
VBOX_LISTENER_DECLARE(VirtualBoxEventListenerImpl)

/**
 * Signal handler that sets g_fGuestCtrlCanceled.
 *
 * This can be executed on any thread in the process, on Windows it may even be
 * a thread dedicated to delivering this signal.  Do not doing anything
 * unnecessary here.
 */
static void signalHandler(int iSignal) RT_NOTHROW_DEF
{
    NOREF(iSignal);
    ASMAtomicWriteBool(&g_fCanceled, true);

    if (g_pEventQ)
    {
        int rc = g_pEventQ->interruptEventQueueProcessing();
        if (RT_FAILURE(rc))
            serviceLog("Error: interruptEventQueueProcessing failed with rc=%Rrc\n", rc);
    }
}

#if 0 /** @todo signal handler installer / uninstallers are unused. */
/**
 * Installs a custom signal handler to get notified
 * whenever the user wants to intercept the program.
 */
static void signalHandlerInstall()
{
    signal(SIGINT,   signalHandler);
#ifdef SIGBREAK
    signal(SIGBREAK, signalHandler);
#endif
}

/**
 * Uninstalls a previously installed signal handler.
 */
static void signalHandlerUninstall()
{
    signal(SIGINT,   SIG_DFL);
#ifdef SIGBREAK
    signal(SIGBREAK, SIG_DFL);
#endif
}
#endif /* unused */

/**
 * Adds a specified machine to the list (map) of handled machines.
 * Does not do locking -- needs to be done by caller!
 *
 * @return  IPRT status code.
 * @param   strUuid                 UUID of the specified machine.
 */
static int machineAdd(const Bstr &strUuid)
{
    HRESULT hrc;

    /** @todo Add exception handling! */

    do
    {
        ComPtr <IMachine> machine;
        CHECK_ERROR_BREAK(g_pVirtualBox, FindMachine(strUuid.raw(), machine.asOutParam()));
        Assert(!machine.isNull());

        /*
         * Get groups for this machine.
        */
        com::SafeArray<BSTR> groups;
        CHECK_ERROR_BREAK(machine, COMGETTER(Groups)(ComSafeArrayAsOutParam(groups)));
        Utf8Str strGroups;
        for (size_t i = 0; i < groups.size(); i++)
        {
            if (i != 0)
                strGroups.append(",");
            strGroups.append(Utf8Str(groups[i]));
        }

        /*
         * Add machine to map.
         */
        VBOXWATCHDOG_MACHINE m;
        m.machine = machine;
        CHECK_ERROR_BREAK(machine, COMGETTER(Name)(m.strName.asOutParam()));

        int rc2 = groupAdd(m.groups, strGroups.c_str(), 0 /* Flags */);
        AssertRC(rc2);

        Assert(g_mapVM.find(strUuid) == g_mapVM.end());
        g_mapVM.insert(std::make_pair(strUuid, m));
        serviceLogVerbose(("Added machine \"%ls\"\n", strUuid.raw()));

        /*
         * Get the machine's VM group(s).
         */
        mapGroupsIterConst itGroup = m.groups.begin();
        while (itGroup != m.groups.end())
        {
            serviceLogVerbose(("Machine \"%ls\" is in VM group \"%s\"\n",
                               strUuid.raw(), itGroup->first.c_str()));

            /* Add machine to group(s). */
            mapGroupIter itGroups = g_mapGroup.find(itGroup->first);
            if (itGroups == g_mapGroup.end())
            {
                vecGroupMembers vecMembers;
                vecMembers.push_back(strUuid);
                g_mapGroup.insert(std::make_pair(itGroup->first, vecMembers));

                itGroups = g_mapGroup.find(itGroup->first);
                Assert(itGroups != g_mapGroup.end());
            }
            else
                itGroups->second.push_back(strUuid);
            serviceLogVerbose(("Group \"%s\" has now %ld machine(s)\n",
                               itGroup->first.c_str(), itGroups->second.size()));
            ++itGroup;
        }

        /*
         * Let all modules know. Typically all modules would register
         * their per-machine payload here.
         */
        for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
            if (g_aModules[j].fEnabled)
            {
                rc2 = g_aModules[j].pDesc->pfnOnMachineRegistered(strUuid);
                if (RT_FAILURE(rc2))
                    serviceLog("OnMachineRegistered: Module '%s' reported an error: %Rrc\n",
                               g_aModules[j].pDesc->pszName, hrc);
                /* Keep going. */
            }

    } while (0);

    /** @todo Add std exception handling! */

    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_COM_IPRT_ERROR; /** @todo Find a better error! */
}

static int machineDestroy(const Bstr &strUuid)
{
    AssertReturn(!strUuid.isEmpty(), VERR_INVALID_PARAMETER);
    int rc = VINF_SUCCESS;

    /* Let all modules know. */
    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
        if (g_aModules[j].fEnabled)
        {
            int rc2 = g_aModules[j].pDesc->pfnOnMachineUnregistered(strUuid);
            if (RT_FAILURE(rc2))
                serviceLog("OnMachineUnregistered: Module '%s' reported an error: %Rrc\n",
                           g_aModules[j].pDesc->pszName, rc);
            /* Keep going. */
        }

    /* Must log before erasing the iterator because of the UUID ref! */
    serviceLogVerbose(("Removing machine \"%ls\"\n", strUuid.raw()));

    try
    {
        mapVMIter itVM = g_mapVM.find(strUuid);
        Assert(itVM != g_mapVM.end());

        /* Remove machine from group(s). */
        mapGroupsIterConst itGroups = itVM->second.groups.begin();
        while (itGroups != itVM->second.groups.end())
        {
            mapGroupIter itGroup = g_mapGroup.find(itGroups->first);
            Assert(itGroup != g_mapGroup.end());

            vecGroupMembers vecMembers = itGroup->second;
            vecGroupMembersIter itMember = std::find(vecMembers.begin(),
                                                     vecMembers.end(),
                                                     strUuid);
            Assert(itMember != vecMembers.end());
            vecMembers.erase(itMember);

            serviceLogVerbose(("Group \"%s\" has %ld machines left\n",
                               itGroup->first.c_str(), vecMembers.size()));
            if (!vecMembers.size())
            {
                serviceLogVerbose(("Deleting group \"%s\"\n", itGroup->first.c_str()));
                g_mapGroup.erase(itGroup);
            }

            ++itGroups;
        }

#ifndef VBOX_WATCHDOG_GLOBAL_PERFCOL
        itVM->second.collector.setNull();
#endif
        itVM->second.machine.setNull();

        /*
         * Remove machine from map.
         */
        g_mapVM.erase(itVM);
    }
    catch (...)
    {
        AssertFailed();
    }

    return rc;
}

/**
 * Removes a specified machine from the list of handled machines.
 * Does not do locking -- needs to be done by caller!
 *
 * @return  IPRT status code.
 * @param   strUuid                 UUID of the specified machine.
 */
static int machineRemove(const Bstr &strUuid)
{
    AssertReturn(!strUuid.isEmpty(), VERR_INVALID_PARAMETER);
    int rc = VINF_SUCCESS;

    mapVMIter it = g_mapVM.find(strUuid);
    if (it != g_mapVM.end())
    {
        int rc2 = machineDestroy(strUuid);
        if (RT_FAILURE(rc))
        {
            serviceLog(("Machine \"%ls\" failed to destroy, rc=%Rc\n"));
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        serviceLogVerbose(("Warning: Removing not added machine \"%ls\"\n", strUuid.raw()));
        rc = VERR_NOT_FOUND;
    }

    return rc;
}

static void vmListDestroy()
{
    serviceLogVerbose(("Destroying VM list ...\n"));

    int rc = RTCritSectEnter(&g_csMachines);
    if (RT_SUCCESS(rc))
    {
        mapVMIter it = g_mapVM.begin();
        while (it != g_mapVM.end())
        {
            machineDestroy(it->first);
            it = g_mapVM.begin();
        }

        g_mapVM.clear();

        rc = RTCritSectLeave(&g_csMachines);
    }
    AssertRC(rc);
}

static int vmListBuild()
{
    serviceLogVerbose(("Building VM list ...\n"));

    int rc = RTCritSectEnter(&g_csMachines);
    if (RT_SUCCESS(rc))
    {
        /*
         * Make sure the list is empty.
         */
        g_mapVM.clear();

        /*
         * Get the list of all _running_ VMs
         */
        com::SafeIfaceArray<IMachine> machines;
        HRESULT hrc = g_pVirtualBox->COMGETTER(Machines)(ComSafeArrayAsOutParam(machines));
        if (SUCCEEDED(hrc))
        {
            /*
             * Iterate through the collection
             */
            for (size_t i = 0; i < machines.size(); ++i)
            {
                if (machines[i])
                {
                    Bstr strUUID;
                    CHECK_ERROR_BREAK(machines[i], COMGETTER(Id)(strUUID.asOutParam()));

                    BOOL fAccessible;
                    CHECK_ERROR_BREAK(machines[i], COMGETTER(Accessible)(&fAccessible));
                    if (!fAccessible)
                    {
                        serviceLogVerbose(("Machine \"%ls\" is inaccessible, skipping\n",
                                           strUUID.raw()));
                        continue;
                    }

                    rc = machineAdd(strUUID);
                    if (RT_FAILURE(rc))
                        break;
                }
            }

            if (!machines.size())
                serviceLogVerbose(("No machines to add found at the moment!\n"));
        }

        int rc2 = RTCritSectLeave(&g_csMachines);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}

/**
 * Lazily calls the pfnPreInit method on each service.
 *
 * @returns VBox status code, error message displayed.
 */
static int watchdogLazyPreInit(void)
{
    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
        if (!g_aModules[j].fPreInited)
        {
            int rc = g_aModules[j].pDesc->pfnPreInit();
            if (RT_FAILURE(rc))
            {
                serviceLog("Module '%s' failed pre-init: %Rrc\n",
                           g_aModules[j].pDesc->pszName, rc);
                return rc;
            }
            g_aModules[j].fPreInited = true;
        }
    return VINF_SUCCESS;
}

/**
 * Starts all registered modules.
 *
 * @return  IPRT status code.
 * @return  int
 */
static int watchdogStartModules()
{
    int rc = VINF_SUCCESS;

    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
    {
        const PVBOXWATCHDOGMOD pMod = &g_aModules[j];
        if (pMod->fEnabled)
        {
            int rc2 = pMod->pDesc->pfnInit();
            if (RT_FAILURE(rc2))
            {
                if (rc2 != VERR_SERVICE_DISABLED)
                {
                    serviceLog("Module '%s' failed to initialize: %Rrc\n", pMod->pDesc->pszName, rc2);
                    return rc;
                }
                pMod->fEnabled = false;
                serviceLog("Module '%s' was disabled because of missing functionality\n", pMod->pDesc->pszName);

            }
        }
        else
            serviceLog("Module '%s' disabled, skipping ...\n", pMod->pDesc->pszName);
    }

    return rc;
}

static int watchdogShutdownModules()
{
    int rc = VINF_SUCCESS;

    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
        if (g_aModules[j].fEnabled)
        {
            int rc2 = g_aModules[j].pDesc->pfnStop();
            if (RT_FAILURE(rc2))
            {
                serviceLog("Module '%s' failed to stop: %Rrc\n",
                           g_aModules[j].pDesc->pszName, rc);
                /* Keep original rc. */
                if (RT_SUCCESS(rc))
                    rc = rc2;
            }
            /* Keep going. */
        }

    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
        if (g_aModules[j].fEnabled)
        {
            g_aModules[j].pDesc->pfnTerm();
        }

    return rc;
}

static RTEXITCODE watchdogMain(/*HandlerArg *a */)
{
    HRESULT hrc = S_OK;

    do
    {
        /* Initialize global weak references. */
        g_pEventQ = com::NativeEventQueue::getMainEventQueue();

        /*
         * Install signal handlers.
         */
        signal(SIGINT,   signalHandler);
#ifdef SIGBREAK
        signal(SIGBREAK, signalHandler);
#endif

        /*
         * Setup the global event listeners:
         * - g_pEventSource for machine events
         * - g_pEventSourceClient for VBoxClient events (like VBoxSVC handling)
         */
        CHECK_ERROR_BREAK(g_pVirtualBox, COMGETTER(EventSource)(g_pEventSource.asOutParam()));
        CHECK_ERROR_BREAK(g_pVirtualBoxClient, COMGETTER(EventSource)(g_pEventSourceClient.asOutParam()));

        ComObjPtr<VirtualBoxEventListenerImpl> vboxListenerImpl;
        vboxListenerImpl.createObject();
        vboxListenerImpl->init(new VirtualBoxEventListener());

        com::SafeArray<VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnMachineRegistered);
        eventTypes.push_back(VBoxEventType_OnMachineStateChanged);
        eventTypes.push_back(VBoxEventType_OnVBoxSVCAvailabilityChanged); /* Processed by g_pEventSourceClient. */

        g_pVBoxEventListener = vboxListenerImpl;
        CHECK_ERROR_BREAK(g_pEventSource, RegisterListener(g_pVBoxEventListener, ComSafeArrayAsInParam(eventTypes), true /* Active listener */));
        CHECK_ERROR_BREAK(g_pEventSourceClient, RegisterListener(g_pVBoxEventListener, ComSafeArrayAsInParam(eventTypes), true /* Active listener */));

        /*
         * Set up modules.
         */
        int vrc = watchdogStartModules();
        if (RT_FAILURE(vrc))
            break;

        for (;;)
        {
            /*
             * Do the actual work.
             */

            vrc = RTCritSectEnter(&g_csMachines);
            if (RT_SUCCESS(vrc))
            {
                for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
                    if (g_aModules[j].fEnabled)
                    {
                        int rc2 = g_aModules[j].pDesc->pfnMain();
                        if (RT_FAILURE(rc2))
                            serviceLog("Module '%s' reported an error: %Rrc\n",
                                       g_aModules[j].pDesc->pszName, rc2);
                        /* Keep going. */
                    }

                int rc2 = RTCritSectLeave(&g_csMachines);
                if (RT_SUCCESS(vrc))
                    vrc = rc2;
                AssertRC(vrc);
            }

            /*
             * Process pending events, then wait for new ones. Note, this
             * processes NULL events signalling event loop termination.
             */
            g_pEventQ->processEventQueue(50);

            if (g_fCanceled)
            {
                serviceLog("Signal caught, exiting ...\n");
                break;
            }
        }

        signal(SIGINT,   SIG_DFL);
    #ifdef SIGBREAK
        signal(SIGBREAK, SIG_DFL);
    #endif

        /* VirtualBox callback unregistration. */
        if (g_pVBoxEventListener)
        {
            if (!g_pEventSource.isNull())
                CHECK_ERROR(g_pEventSource, UnregisterListener(g_pVBoxEventListener));
            g_pVBoxEventListener.setNull();
        }

        g_pEventSource.setNull();
        g_pEventSourceClient.setNull();

        vrc = watchdogShutdownModules();
        AssertRC(vrc);

        if (RT_FAILURE(vrc))
            hrc = VBOX_E_IPRT_ERROR;

    } while (0);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

void serviceLog(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    char *psz = NULL;
    RTStrAPrintfV(&psz, pszFormat, args);
    va_end(args);

    LogRel(("%s", psz));

    RTStrFree(psz);
}

static void displayHeader()
{
    RTStrmPrintf(g_pStdErr, VBOX_PRODUCT " Watchdog " VBOX_VERSION_STRING "\n"
                 "Copyright (C) " VBOX_C_YEAR " " VBOX_VENDOR "\n\n");
}

/**
 * Displays the help.
 *
 * @param   pszImage                Name of program name (image).
 */
static void displayHelp(const char *pszImage)
{
    AssertPtrReturnVoid(pszImage);

    displayHeader();

    RTStrmPrintf(g_pStdErr,
                 "Usage: %s [-v|--verbose] [-h|-?|--help] [-P|--pidfile]\n"
                 "           [-F|--logfile=<file>] [-R|--logrotate=<num>] \n"
                 "           [-S|--logsize=<bytes>] [-I|--loginterval=<seconds>]\n",
                 pszImage);
    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
        if (g_aModules[j].pDesc->pszUsage)
            RTStrmPrintf(g_pStdErr, "%s", g_aModules[j].pDesc->pszUsage);

    RTStrmPrintf(g_pStdErr,
                 "\n"
                 "Options:\n");

    for (unsigned i = 0; i < RT_ELEMENTS(g_aOptions); i++)
    {
        const char *pcszDescr;
        switch (g_aOptions[i].iShort)
        {
            case GETOPTDEF_WATCHDOG_DISABLE_MODULE:
                pcszDescr = "Disables a module. See module list for built-in modules.";
                break;

            case GETOPTDEF_WATCHDOG_DRYRUN:
                pcszDescr = "Dryrun mode -- do not perform any actions.";
                break;

            case 'h':
                pcszDescr = "Print this help message and exit.";
                break;

#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
            case 'b':
                pcszDescr = "Run in background (daemon mode).";
                break;
#endif
            case 'P':
                pcszDescr = "Name of the PID file which is created when the daemon was started.";
                break;

            case 'F':
                pcszDescr = "Name of file to write log to (no file).";
                break;

            case 'R':
                pcszDescr = "Number of log files (0 disables log rotation).";
                break;

            case 'S':
                pcszDescr = "Maximum size of a log file to trigger rotation (bytes).";
                break;

            case 'I':
                pcszDescr = "Maximum time interval to trigger log rotation (seconds).";
                break;
            default:
                AssertFailedBreakStmt(pcszDescr = "");
        }

        if (g_aOptions[i].iShort < 1000)
            RTStrmPrintf(g_pStdErr,
                         "  %s, -%c\n"
                         "      %s\n", g_aOptions[i].pszLong, g_aOptions[i].iShort, pcszDescr);
        else
            RTStrmPrintf(g_pStdErr,
                         "  %s\n"
                         "      %s\n", g_aOptions[i].pszLong, pcszDescr);
    }

    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
    {
        if (g_aModules[j].pDesc->pszOptions)
            RTStrmPrintf(g_pStdErr, "%s", g_aModules[j].pDesc->pszOptions);
    }

    /** @todo Change VBOXBALLOONCTRL_RELEASE_LOG to WATCHDOG*. */
    RTStrmPrintf(g_pStdErr, "\nUse environment variable VBOXBALLOONCTRL_RELEASE_LOG for logging options.\n");

    RTStrmPrintf(g_pStdErr, "\nValid module names are: ");
    for (unsigned j = 0; j < RT_ELEMENTS(g_aModules); j++)
    {
        if (j > 0)
            RTStrmPrintf(g_pStdErr, ", ");
        RTStrmPrintf(g_pStdErr, "%s", g_aModules[j].pDesc->pszName);
    }
    RTStrmPrintf(g_pStdErr, "\n\n");
}

/**
 * Creates all global COM objects.
 *
 * @return  HRESULT
 */
static int watchdogSetup()
{
    serviceLogVerbose(("Setting up ...\n"));

    /*
     * Setup VirtualBox + session interfaces.
     */
    HRESULT hrc = g_pVirtualBoxClient->COMGETTER(VirtualBox)(g_pVirtualBox.asOutParam());
    if (SUCCEEDED(hrc))
    {
        hrc = g_pSession.createInprocObject(CLSID_Session);
        if (FAILED(hrc))
            RTMsgError("Failed to create a session object (rc=%Rhrc)!", hrc);
    }
    else
        RTMsgError("Failed to get VirtualBox object (rc=%Rhrc)!", hrc);

    if (FAILED(hrc))
        return VERR_COM_OBJECT_NOT_FOUND;

    /*
     * Setup metrics.
     */
#ifdef VBOX_WATCHDOG_GLOBAL_PERFCOL
    CHECK_ERROR_RET(g_pVirtualBox,
                    COMGETTER(PerformanceCollector)(g_pPerfCollector.asOutParam()), VERR_COM_UNEXPECTED);
#endif

    int vrc = RTCritSectInit(&g_csMachines);
    if (RT_SUCCESS(vrc))
    {

        /*
         * Build up initial VM list.
         */
        vrc = vmListBuild();
    }

    return vrc;
}

static void watchdogShutdown()
{
    serviceLogVerbose(("Shutting down ...\n"));

    vmListDestroy();

    int rc = RTCritSectDelete(&g_csMachines);
    AssertRC(rc);

#ifdef VBOX_WATCHDOG_GLOBAL_PERFCOL
    g_pPerfCollector.setNull();
#endif

    g_pSession.setNull();
    g_pVirtualBox.setNull();
}

int main(int argc, char *argv[])
{
    /*
     * Before we do anything, init the runtime without loading
     * the support driver.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
#ifdef RT_OS_WINDOWS
    ATL::CComModule _Module; /* Required internally by ATL (constructor records instance in global variable). */
#endif

    /*
     * Parse the global options
     */
    int c;
    const char *pszLogFile = NULL;
    const char *pszPidFile = NULL;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv,
                 g_aOptions, RT_ELEMENTS(g_aOptions), 1 /* First */, 0 /*fFlags*/);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case GETOPTDEF_WATCHDOG_DRYRUN:
                g_fDryrun = true;
                break;

            case 'h':
                displayHelp(argv[0]);
                return 0;

            case 'v':
                g_fVerbose = true;
                break;

#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
            case 'b':
                g_fDaemonize = true;
                break;
#endif
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return 0;

            case 'P':
                pszPidFile = ValueUnion.psz;
                break;

            case 'F':
                pszLogFile = ValueUnion.psz;
                break;

            case 'R':
                g_cHistory = ValueUnion.u32;
                break;

            case 'S':
                g_uHistoryFileSize = ValueUnion.u64;
                break;

            case 'I':
                g_uHistoryFileTime = ValueUnion.u32;
                break;

            default:
            {
                bool fFound = false;

                char szModDisable[64];
                for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aModules); j++)
                {
                    if (!RTStrPrintf(szModDisable, sizeof(szModDisable), "--disable-%s", g_aModules[j].pDesc->pszName))
                        continue;

                    if (!RTStrICmp(szModDisable, ValueUnion.psz))
                    {
                        g_aModules[j].fEnabled = false;
                        fFound = true;
                    }
                }

                if (!fFound)
                {
                    rc = watchdogLazyPreInit();
                    if (RT_FAILURE(rc))
                        return RTEXITCODE_FAILURE;

                    for (unsigned j = 0; !fFound && j < RT_ELEMENTS(g_aModules); j++)
                    {
                        if (!g_aModules[j].fEnabled)
                            continue;

                        int iArgCnt = argc - GetState.iNext + 1;
                        int iArgIndex = GetState.iNext - 1;
                        int iConsumed = 0;
                        rc = g_aModules[j].pDesc->pfnOption(iArgCnt,
                                                            &argv[iArgIndex],
                                                            &iConsumed);
                        fFound = rc == 0;
                        if (fFound)
                        {
                            GetState.iNext += iConsumed;
                            break;
                        }
                        if (rc != -1)
                            return rc;
                    }
                }
                if (!fFound)
                    return RTGetOptPrintError(c, &ValueUnion);
                continue;
            }
        }
    }

    /** @todo Add "--quiet/-q" option to not show the header. */
    displayHeader();

    /* create release logger, to stdout */
    RTERRINFOSTATIC ErrInfo;
    rc = com::VBoxLogRelCreate("Watchdog", g_fDaemonize ? NULL : pszLogFile,
                               RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG,
                               "all", "VBOXBALLOONCTRL_RELEASE_LOG",
                               RTLOGDEST_STDOUT, UINT32_MAX /* cMaxEntriesPerGroup */,
                               g_cHistory, g_uHistoryFileTime, g_uHistoryFileSize,
                               RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to open release log (%s, %Rrc)", ErrInfo.Core.pszMsg, rc);

#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX) || defined (RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
    if (g_fDaemonize)
    {
        /* prepare release logging */
        char szLogFile[RTPATH_MAX];

        if (!pszLogFile || !*pszLogFile)
        {
            rc = com::GetVBoxUserHomeDirectory(szLogFile, sizeof(szLogFile));
            if (RT_FAILURE(rc))
                 return RTMsgErrorExit(RTEXITCODE_FAILURE, "could not get base directory for logging: %Rrc", rc);
            rc = RTPathAppend(szLogFile, sizeof(szLogFile), "vboxballoonctrl.log");
            if (RT_FAILURE(rc))
                 return RTMsgErrorExit(RTEXITCODE_FAILURE, "could not construct logging path: %Rrc", rc);
            pszLogFile = szLogFile;
        }

        rc = RTProcDaemonizeUsingFork(false /* fNoChDir */, false /* fNoClose */, pszPidFile);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to daemonize, rc=%Rrc. exiting.", rc);
        /* create release logger, to file */
        rc = com::VBoxLogRelCreate("Watchdog", pszLogFile,
                                   RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG,
                                   "all", "VBOXBALLOONCTRL_RELEASE_LOG",
                                   RTLOGDEST_FILE, UINT32_MAX /* cMaxEntriesPerGroup */,
                                   g_cHistory, g_uHistoryFileTime, g_uHistoryFileSize,
                                   RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "failed to open release log (%s, %Rrc)", ErrInfo.Core.pszMsg, rc);
    }
#endif

#ifndef VBOX_ONLY_DOCS
    /*
     * Initialize COM.
     */
    using namespace com;
    HRESULT hrc = com::Initialize();
# ifdef VBOX_WITH_XPCOM
    if (hrc == NS_ERROR_FILE_ACCESS_DENIED)
    {
        char szHome[RTPATH_MAX] = "";
        com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
        return RTMsgErrorExit(RTEXITCODE_FAILURE,
               "Failed to initialize COM because the global settings directory '%s' is not accessible!", szHome);
    }
# endif
    if (FAILED(hrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to initialize COM (%Rhrc)!", hrc);

    hrc = g_pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
    if (FAILED(hrc))
    {
        RTMsgError("Failed to create the VirtualBoxClient object (%Rhrc)!", hrc);
        com::ErrorInfo info;
        if (!info.isFullAvailable() && !info.isBasicAvailable())
        {
            com::GluePrintRCMessage(hrc);
            RTMsgError("Most likely, the VirtualBox COM server is not running or failed to start.");
        }
        else
            com::GluePrintErrorInfo(info);
        return RTEXITCODE_FAILURE;
    }

    if (g_fDryrun)
        serviceLog("Running in dryrun mode\n");

    rc = watchdogSetup();
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    //HandlerArg handlerArg = { argc, argv };
    RTEXITCODE rcExit = watchdogMain(/*&handlerArg*/);

    NativeEventQueue::getMainEventQueue()->processEventQueue(0);

    watchdogShutdown();

    g_pVirtualBoxClient.setNull();

    com::Shutdown();

    return rcExit;
#else  /* VBOX_ONLY_DOCS */
    return RTEXITCODE_SUCCESS;
#endif /* VBOX_ONLY_DOCS */
}

