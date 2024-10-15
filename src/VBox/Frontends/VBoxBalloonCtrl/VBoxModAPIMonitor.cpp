/* $Id: VBoxModAPIMonitor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxModAPIMonitor - API monitor module for detecting host isolation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
# include <iprt/message.h>
# include <VBox/com/errorprint.h>
#endif /* !VBOX_ONLY_DOCS */

#include "VBoxWatchdogInternal.h"

using namespace com;

#define VBOX_MOD_APIMON_NAME "apimon"

/**
 * The module's RTGetOpt-IDs for the command line.
 */
enum GETOPTDEF_APIMON
{
    GETOPTDEF_APIMON_GROUPS = 3000,
    GETOPTDEF_APIMON_ISLN_RESPONSE,
    GETOPTDEF_APIMON_ISLN_TIMEOUT,
    GETOPTDEF_APIMON_RESP_TIMEOUT
};

/**
 * The module's command line arguments.
 */
static const RTGETOPTDEF g_aAPIMonitorOpts[] = {
    { "--apimon-groups",            GETOPTDEF_APIMON_GROUPS,         RTGETOPT_REQ_STRING },
    { "--apimon-isln-response",     GETOPTDEF_APIMON_ISLN_RESPONSE,  RTGETOPT_REQ_STRING },
    { "--apimon-isln-timeout",      GETOPTDEF_APIMON_ISLN_TIMEOUT,   RTGETOPT_REQ_UINT32 },
    { "--apimon-resp-timeout",      GETOPTDEF_APIMON_RESP_TIMEOUT,   RTGETOPT_REQ_UINT32 }
};

enum APIMON_RESPONSE
{
    /** Unknown / unhandled response. */
    APIMON_RESPONSE_NONE       = 0,
    /** Pauses the VM execution. */
    APIMON_RESPONSE_PAUSE      = 10,
    /** Does a hard power off. */
    APIMON_RESPONSE_POWEROFF   = 200,
    /** Tries to save the current machine state. */
    APIMON_RESPONSE_SAVE       = 250,
    /** Tries to shut down all running VMs in
     *  a gentle manner. */
    APIMON_RESPONSE_SHUTDOWN   = 300
};

/** The VM group(s) the API monitor handles. If none, all VMs get handled. */
static mapGroups                    g_vecAPIMonGroups; /** @todo Move this into module payload! */
static APIMON_RESPONSE              g_enmAPIMonIslnResp = APIMON_RESPONSE_NONE;
static uint32_t                     g_cMsAPIMonIslnTimeout = 0;
static Bstr                         g_strAPIMonIslnLastBeat;
static uint32_t                     g_cMsAPIMonResponseTimeout = 0;
static uint64_t                     g_uAPIMonIslnLastBeatMS = 0;

static int apimonResponseToEnum(const char *pszResponse, APIMON_RESPONSE *pResp)
{
    AssertPtrReturn(pszResponse, VERR_INVALID_POINTER);
    AssertPtrReturn(pResp, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    if (!RTStrICmp(pszResponse, "none"))
    {
        *pResp = APIMON_RESPONSE_NONE;
    }
    else if (!RTStrICmp(pszResponse, "pause"))
    {
        *pResp = APIMON_RESPONSE_PAUSE;
    }
    else if (   !RTStrICmp(pszResponse, "poweroff")
             || !RTStrICmp(pszResponse, "powerdown"))
    {
        *pResp = APIMON_RESPONSE_POWEROFF;
    }
    else if (!RTStrICmp(pszResponse, "save"))
    {
        *pResp = APIMON_RESPONSE_SAVE;
    }
    else if (   !RTStrICmp(pszResponse, "shutdown")
             || !RTStrICmp(pszResponse, "shutoff"))
    {
        *pResp = APIMON_RESPONSE_SHUTDOWN;
    }
    else
    {
        *pResp = APIMON_RESPONSE_NONE;
        rc = VERR_INVALID_PARAMETER;
    }

    return rc;
}

static const char* apimonResponseToStr(APIMON_RESPONSE enmResp)
{
    if (APIMON_RESPONSE_NONE == enmResp)
        return "none";
    else if (APIMON_RESPONSE_PAUSE == enmResp)
        return "pausing";
    else if (APIMON_RESPONSE_POWEROFF == enmResp)
        return "powering off";
    else if (APIMON_RESPONSE_SAVE == enmResp)
        return "saving state";
    else if (APIMON_RESPONSE_SHUTDOWN == enmResp)
        return "shutting down";

    return "unknown";
}

/* Copied from VBoxManageInfo.cpp. */
static const char *apimonMachineStateToName(MachineState_T machineState, bool fShort)
{
    switch (machineState)
    {
        case MachineState_PoweredOff:
            return fShort ? "poweroff"             : "powered off";
        case MachineState_Saved:
            return "saved";
        case MachineState_Teleported:
            return "teleported";
        case MachineState_Aborted:
            return "aborted";
        case MachineState_AbortedSaved:
            return "aborted-saved";
        case MachineState_Running:
            return "running";
        case MachineState_Paused:
            return "paused";
        case MachineState_Stuck:
            return fShort ? "gurumeditation"       : "guru meditation";
        case MachineState_LiveSnapshotting:
            return fShort ? "livesnapshotting"     : "live snapshotting";
        case MachineState_Teleporting:
            return "teleporting";
        case MachineState_Starting:
            return "starting";
        case MachineState_Stopping:
            return "stopping";
        case MachineState_Saving:
            return "saving";
        case MachineState_Restoring:
            return "restoring";
        case MachineState_TeleportingPausedVM:
            return fShort ? "teleportingpausedvm"  : "teleporting paused vm";
        case MachineState_TeleportingIn:
            return fShort ? "teleportingin"        : "teleporting (incoming)";
        case MachineState_RestoringSnapshot:
            return fShort ? "restoringsnapshot"    : "restoring snapshot";
        case MachineState_DeletingSnapshot:
            return fShort ? "deletingsnapshot"     : "deleting snapshot";
        case MachineState_DeletingSnapshotOnline:
            return fShort ? "deletingsnapshotlive" : "deleting snapshot live";
        case MachineState_DeletingSnapshotPaused:
            return fShort ? "deletingsnapshotlivepaused" : "deleting snapshot live paused";
        case MachineState_SettingUp:
            return fShort ? "settingup"           : "setting up";
        default:
            break;
    }
    return "unknown";
}

static int apimonMachineControl(const Bstr &strUuid, PVBOXWATCHDOG_MACHINE pMachine,
                                APIMON_RESPONSE enmResp, uint32_t cMsTimeout)
{
    /** @todo Add other commands (with enmResp) here. */
    AssertPtrReturn(pMachine, VERR_INVALID_POINTER);

    serviceLogVerbose(("apimon: Triggering \"%s\" (%RU32ms timeout) for machine \"%ls\"\n",
                      apimonResponseToStr(enmResp), cMsTimeout, strUuid.raw()));

    if (   enmResp == APIMON_RESPONSE_NONE
        || g_fDryrun)
        return VINF_SUCCESS; /* Nothing to do. */

    HRESULT hrc;
    ComPtr <IMachine> machine;
    CHECK_ERROR_RET(g_pVirtualBox, FindMachine(strUuid.raw(),
                                               machine.asOutParam()), VERR_NOT_FOUND);
    do
    {
        /* Query the machine's state to avoid unnecessary IPC. */
        MachineState_T machineState;
        CHECK_ERROR_BREAK(machine, COMGETTER(State)(&machineState));

        if (   machineState == MachineState_Running
            || machineState == MachineState_Paused)
        {
            /* Open a session for the VM. */
            CHECK_ERROR_BREAK(machine, LockMachine(g_pSession, LockType_Shared));

            do
            {
                /* Get the associated console. */
                ComPtr<IConsole> console;
                CHECK_ERROR_BREAK(g_pSession, COMGETTER(Console)(console.asOutParam()));
                /* Get the associated session machine. */
                ComPtr<IMachine> sessionMachine;
                CHECK_ERROR_BREAK(g_pSession, COMGETTER(Machine)(sessionMachine.asOutParam()));

                ComPtr<IProgress> progress;

                switch (enmResp)
                {
                    case APIMON_RESPONSE_PAUSE:
                        if (machineState != MachineState_Paused)
                        {
                            serviceLogVerbose(("apimon: Pausing machine \"%ls\" ...\n",
                                               strUuid.raw()));
                            CHECK_ERROR_BREAK(console, Pause());
                        }
                        break;

                    case APIMON_RESPONSE_POWEROFF:
                        serviceLogVerbose(("apimon: Powering off machine \"%ls\" ...\n",
                                           strUuid.raw()));
                        CHECK_ERROR_BREAK(console, PowerDown(progress.asOutParam()));
                        progress->WaitForCompletion(cMsTimeout);
                        CHECK_PROGRESS_ERROR(progress, ("Failed to power off machine \"%ls\"",
                                             strUuid.raw()));
                        break;

                    case APIMON_RESPONSE_SAVE:
                    {
                        serviceLogVerbose(("apimon: Saving state of machine \"%ls\" ...\n",
                                           strUuid.raw()));

                        /* First pause so we don't trigger a live save which needs more time/resources. */
                        bool fPaused = false;
                        hrc = console->Pause();
                        if (FAILED(hrc))
                        {
                            bool fError = true;
                            if (hrc == VBOX_E_INVALID_VM_STATE)
                            {
                                /* Check if we are already paused. */
                                CHECK_ERROR_BREAK(console, COMGETTER(State)(&machineState));
                                /* The error code was lost by the previous instruction. */
                                hrc = VBOX_E_INVALID_VM_STATE;
                                if (machineState != MachineState_Paused)
                                {
                                    serviceLog("apimon: Machine \"%ls\" in invalid state %d -- %s\n",
                                               strUuid.raw(), machineState, apimonMachineStateToName(machineState, false));
                                }
                                else
                                {
                                    fError = false;
                                    fPaused = true;
                                }
                            }
                            if (fError)
                                break;
                        }

                        CHECK_ERROR(sessionMachine, SaveState(progress.asOutParam()));
                        if (SUCCEEDED(hrc))
                        {
                            progress->WaitForCompletion(cMsTimeout);
                            CHECK_PROGRESS_ERROR(progress, ("Failed to save machine state of machine \"%ls\"",
                                                 strUuid.raw()));
                        }

                        if (SUCCEEDED(hrc))
                        {
                            serviceLogVerbose(("apimon: State of machine \"%ls\" saved, powering off ...\n", strUuid.raw()));
                            CHECK_ERROR_BREAK(console, PowerButton());
                        }
                        else
                            serviceLogVerbose(("apimon: Saving state of machine \"%ls\" failed\n", strUuid.raw()));

                        break;
                    }

                    case APIMON_RESPONSE_SHUTDOWN:
                        serviceLogVerbose(("apimon: Shutting down machine \"%ls\" ...\n", strUuid.raw()));
                        CHECK_ERROR_BREAK(console, PowerButton());
                        break;

                    default:
                        AssertMsgFailed(("Response %d not implemented", enmResp));
                        break;
                }
            } while (0);

            /* Unlock the machine again. */
            g_pSession->UnlockMachine();
        }
        else
            serviceLogVerbose(("apimon: Warning: Could not trigger \"%s\" (%d) for machine \"%ls\"; in state \"%s\" (%d) currently\n",
                               apimonResponseToStr(enmResp), enmResp, strUuid.raw(),
                               apimonMachineStateToName(machineState, false), machineState));
    } while (0);



    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_COM_IPRT_ERROR;
}

static bool apimonHandleVM(const PVBOXWATCHDOG_MACHINE pMachine)
{
    bool fHandleVM = false;

    try
    {
        mapGroupsIterConst itVMGroup = pMachine->groups.begin();
        while (   itVMGroup != pMachine->groups.end()
               && !fHandleVM)
        {
            mapGroupsIterConst itInGroup = g_vecAPIMonGroups.find(itVMGroup->first);
            if (itInGroup != g_vecAPIMonGroups.end())
                fHandleVM = true;

            ++itVMGroup;
        }
    }
    catch (...)
    {
        AssertFailed();
    }

    return fHandleVM;
}

static int apimonTrigger(APIMON_RESPONSE enmResp)
{
    int rc = VINF_SUCCESS;

    bool fAllGroups = g_vecAPIMonGroups.empty();
    mapVMIter it = g_mapVM.begin();

    if (it == g_mapVM.end())
    {
        serviceLog("apimon: No machines in list, skipping ...\n");
        return rc;
    }

    while (it != g_mapVM.end())
    {
        bool fHandleVM = fAllGroups;
        try
        {
            if (!fHandleVM)
                fHandleVM = apimonHandleVM(&it->second);

            if (fHandleVM)
            {
                int rc2 = apimonMachineControl(it->first /* Uuid */,
                                               &it->second /* Machine */, enmResp, g_cMsAPIMonResponseTimeout);
                if (RT_FAILURE(rc2))
                    serviceLog("apimon: Controlling machine \"%ls\" (response \"%s\") failed with rc=%Rrc",
                               it->first.raw(), apimonResponseToStr(enmResp), rc);

                if (RT_SUCCESS(rc))
                    rc = rc2; /* Store original error. */
                /* Keep going. */
            }
        }
        catch (...)
        {
            AssertFailed();
        }

        ++it;
    }

    return rc;
}

/* Callbacks. */
static DECLCALLBACK(int) VBoxModAPIMonitorPreInit(void)
{
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) VBoxModAPIMonitorOption(int argc, char *argv[], int *piConsumed)
{
    if (!argc) /* Take a shortcut. */
        return -1;

    AssertPtrReturn(argv, VERR_INVALID_POINTER);
    AssertPtrReturn(piConsumed, VERR_INVALID_POINTER);

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, argc, argv,
                          g_aAPIMonitorOpts, RT_ELEMENTS(g_aAPIMonitorOpts),
                          0 /* First */, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return rc;

    rc = 0; /* Set default parsing result to valid. */

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case GETOPTDEF_APIMON_GROUPS:
            {
                rc = groupAdd(g_vecAPIMonGroups, ValueUnion.psz, 0 /* Flags */);
                if (RT_FAILURE(rc))
                    rc = -1; /* Option unknown. */
                break;
            }

            case GETOPTDEF_APIMON_ISLN_RESPONSE:
                rc = apimonResponseToEnum(ValueUnion.psz, &g_enmAPIMonIslnResp);
                if (RT_FAILURE(rc))
                    rc = -1; /* Option unknown. */
                break;

            case GETOPTDEF_APIMON_ISLN_TIMEOUT:
                g_cMsAPIMonIslnTimeout = ValueUnion.u32;
                if (g_cMsAPIMonIslnTimeout < 1000) /* Don't allow timeouts < 1s. */
                    g_cMsAPIMonIslnTimeout = 1000;
                break;

            case GETOPTDEF_APIMON_RESP_TIMEOUT:
                g_cMsAPIMonResponseTimeout = ValueUnion.u32;
                if (g_cMsAPIMonResponseTimeout < 5000) /* Don't allow timeouts < 5s. */
                    g_cMsAPIMonResponseTimeout = 5000;
                break;

            default:
                rc = -1; /* We don't handle this option, skip. */
                break;
        }

        /* At the moment we only process one option at a time. */
        break;
    }

    *piConsumed += GetState.iNext - 1;

    return rc;
}

static DECLCALLBACK(int) VBoxModAPIMonitorInit(void)
{
    HRESULT hrc = S_OK;

    do
    {
        Bstr strValue;

        /* VM groups to watch for. */
        if (g_vecAPIMonGroups.empty()) /* Not set by command line? */
        {
            CHECK_ERROR_BREAK(g_pVirtualBox, GetExtraData(Bstr("VBoxInternal2/Watchdog/APIMonitor/Groups").raw(),
                                                          strValue.asOutParam()));
            if (!strValue.isEmpty())
            {
                int rc2 = groupAdd(g_vecAPIMonGroups, Utf8Str(strValue).c_str(), 0 /* Flags */);
                if (RT_FAILURE(rc2))
                    serviceLog("apimon: Warning: API monitor groups string invalid (%ls)\n", strValue.raw());
            }
        }

        if (!g_cMsAPIMonIslnTimeout)
            cfgGetValueU32(g_pVirtualBox, NULL /* Machine */,
                           "VBoxInternal2/Watchdog/APIMonitor/IsolationTimeoutMS", NULL /* Per-machine */,
                           &g_cMsAPIMonIslnTimeout, 30 * 1000 /* Default is 30 seconds timeout. */);
        g_cMsAPIMonIslnTimeout = RT_MIN(1000, g_cMsAPIMonIslnTimeout);

        if (g_enmAPIMonIslnResp == APIMON_RESPONSE_NONE) /* Not set by command line? */
        {
            Utf8Str strResp;
            int rc2 = cfgGetValueStr(g_pVirtualBox, NULL /* Machine */,
                                     "VBoxInternal2/Watchdog/APIMonitor/IsolationResponse", NULL /* Per-machine */,
                                     strResp, "" /* Default value. */);
            if (RT_SUCCESS(rc2))
            {
                rc2 = apimonResponseToEnum(strResp.c_str(), &g_enmAPIMonIslnResp);
                if (RT_FAILURE(rc2))
                    serviceLog("apimon: Warning: API monitor response string invalid (%ls), defaulting to no action\n",
                               strValue.raw());
            }
        }

        if (!g_cMsAPIMonResponseTimeout)
            cfgGetValueU32(g_pVirtualBox, NULL /* Machine */,
                           "VBoxInternal2/Watchdog/APIMonitor/ResponseTimeoutMS", NULL /* Per-machine */,
                           &g_cMsAPIMonResponseTimeout, 30 * 1000 /* Default is 30 seconds timeout. */);
        g_cMsAPIMonResponseTimeout = RT_MIN(5000, g_cMsAPIMonResponseTimeout);

#ifdef DEBUG
        /* Groups. */
        serviceLogVerbose(("apimon: Handling %u groups:", g_vecAPIMonGroups.size()));
        mapGroupsIterConst itGroups = g_vecAPIMonGroups.begin();
        while (itGroups != g_vecAPIMonGroups.end())
        {
            serviceLogVerbose((" %s", itGroups->first.c_str()));
            ++itGroups;
        }
        serviceLogVerbose(("\n"));
#endif

    } while (0);

    if (SUCCEEDED(hrc))
    {
        g_uAPIMonIslnLastBeatMS = 0;
    }

    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_COM_IPRT_ERROR; /** @todo Find a better rc! */
}

static DECLCALLBACK(int) VBoxModAPIMonitorMain(void)
{
    static uint64_t uLastRun = 0;
    uint64_t uNow = RTTimeProgramMilliTS();
    uint64_t uDelta = uNow - uLastRun;
    if (uDelta < 1000) /* Only check every second (or later). */
        return VINF_SUCCESS;
    uLastRun = uNow;

    int vrc = VINF_SUCCESS;
    HRESULT hrc;

#ifdef DEBUG
    serviceLogVerbose(("apimon: Checking for API heartbeat (%RU64ms) ...\n",
                       g_cMsAPIMonIslnTimeout));
#endif

    do
    {
        Bstr strHeartbeat;
        CHECK_ERROR_BREAK(g_pVirtualBox, GetExtraData(Bstr("Watchdog/APIMonitor/Heartbeat").raw(),
                                                      strHeartbeat.asOutParam()));
        if (   SUCCEEDED(hrc)
            && !strHeartbeat.isEmpty()
            && g_strAPIMonIslnLastBeat.compare(strHeartbeat, Bstr::CaseSensitive))
        {
            serviceLogVerbose(("apimon: API heartbeat received, resetting timeout\n"));

            g_uAPIMonIslnLastBeatMS = 0;
            g_strAPIMonIslnLastBeat = strHeartbeat;
        }
        else
        {
            g_uAPIMonIslnLastBeatMS += uDelta;
            if (g_uAPIMonIslnLastBeatMS > g_cMsAPIMonIslnTimeout)
            {
                serviceLogVerbose(("apimon: No API heartbeat within time received (%RU64ms)\n",
                                   g_cMsAPIMonIslnTimeout));

                vrc = apimonTrigger(g_enmAPIMonIslnResp);
                g_uAPIMonIslnLastBeatMS = 0;
            }
        }
    } while (0);

    if (FAILED(hrc))
        vrc = VERR_COM_IPRT_ERROR;

    return vrc;
}

static DECLCALLBACK(int) VBoxModAPIMonitorStop(void)
{
    return VINF_SUCCESS;
}

static DECLCALLBACK(void) VBoxModAPIMonitorTerm(void)
{
}

static DECLCALLBACK(int) VBoxModAPIMonitorOnMachineRegistered(const Bstr &strUuid)
{
    RT_NOREF(strUuid);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) VBoxModAPIMonitorOnMachineUnregistered(const Bstr &strUuid)
{
    RT_NOREF(strUuid);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) VBoxModAPIMonitorOnMachineStateChanged(const Bstr &strUuid, MachineState_T enmState)
{
    RT_NOREF(strUuid, enmState);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) VBoxModAPIMonitorOnServiceStateChanged(bool fAvailable)
{
    if (!fAvailable)
    {
        serviceLog(("apimon: VBoxSVC became unavailable, triggering action\n"));
        return apimonTrigger(g_enmAPIMonIslnResp);
    }
    return VINF_SUCCESS;
}

/**
 * The 'apimonitor' module description.
 */
VBOXMODULE g_ModAPIMonitor =
{
    /* pszName. */
    VBOX_MOD_APIMON_NAME,
    /* pszDescription. */
    "API monitor for host isolation detection",
    /* pszDepends. */
    NULL,
    /* uPriority. */
    0 /* Not used */,
    /* pszUsage. */
    "           [--apimon-groups=<string[,stringN]>]\n"
    "           [--apimon-isln-response=<cmd>] [--apimon-isln-timeout=<ms>]\n"
    "           [--apimon-resp-timeout=<ms>]",
    /* pszOptions. */
    "  --apimon-groups=<string[,...]>\n"
    "      Sets the VM groups for monitoring (all), comma-separated list.\n"
    "  --apimon-isln-response=<cmd>\n"
    "      Sets the isolation response to one of: none, pause, poweroff,\n"
    "      save, or shutdown.  Default: none\n"
    "  --apimon-isln-timeout=<ms>\n"
    "      Sets the isolation timeout in ms (30s).\n"
    "  --apimon-resp-timeout=<ms>\n"
    "      Sets the response timeout in ms (30s).\n",
    /* methods. */
    VBoxModAPIMonitorPreInit,
    VBoxModAPIMonitorOption,
    VBoxModAPIMonitorInit,
    VBoxModAPIMonitorMain,
    VBoxModAPIMonitorStop,
    VBoxModAPIMonitorTerm,
    /* callbacks. */
    VBoxModAPIMonitorOnMachineRegistered,
    VBoxModAPIMonitorOnMachineUnregistered,
    VBoxModAPIMonitorOnMachineStateChanged,
    VBoxModAPIMonitorOnServiceStateChanged
};

