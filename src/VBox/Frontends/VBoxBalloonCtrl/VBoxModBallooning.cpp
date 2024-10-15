/* $Id: VBoxModBallooning.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxModBallooning - Module for handling the automatic ballooning of VMs.
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
# include <VBox/com/errorprint.h>
#endif /* !VBOX_ONLY_DOCS */

#include "VBoxWatchdogInternal.h"
#include <iprt/system.h>

using namespace com;

#define VBOX_MOD_BALLOONING_NAME "balloon"


/*********************************************************************************************************************************
*   Local Structures                                                                                                             *
*********************************************************************************************************************************/

/**
 * The module's RTGetOpt-IDs for the command line.
 */
enum GETOPTDEF_BALLOONCTRL
{
    GETOPTDEF_BALLOONCTRL_BALLOONINC = 2000,
    GETOPTDEF_BALLOONCTRL_BALLOONDEC,
    GETOPTDEF_BALLOONCTRL_BALLOONLOWERLIMIT,
    GETOPTDEF_BALLOONCTRL_BALLOONMAX,
    GETOPTDEF_BALLOONCTRL_BALLOONSAFETY,
    GETOPTDEF_BALLOONCTRL_TIMEOUTMS,
    GETOPTDEF_BALLOONCTRL_GROUPS
};

/**
 * The module's command line arguments.
 */
static const RTGETOPTDEF g_aBalloonOpts[] = {
    { "--balloon-dec",            GETOPTDEF_BALLOONCTRL_BALLOONDEC,        RTGETOPT_REQ_UINT32 },
    { "--balloon-groups",         GETOPTDEF_BALLOONCTRL_GROUPS,            RTGETOPT_REQ_STRING },
    { "--balloon-inc",            GETOPTDEF_BALLOONCTRL_BALLOONINC,        RTGETOPT_REQ_UINT32 },
    { "--balloon-interval",       GETOPTDEF_BALLOONCTRL_TIMEOUTMS,         RTGETOPT_REQ_UINT32 },
    { "--balloon-lower-limit",    GETOPTDEF_BALLOONCTRL_BALLOONLOWERLIMIT, RTGETOPT_REQ_UINT32 },
    { "--balloon-max",            GETOPTDEF_BALLOONCTRL_BALLOONMAX,        RTGETOPT_REQ_UINT32 },
    { "--balloon-safety-margin",  GETOPTDEF_BALLOONCTRL_BALLOONSAFETY,     RTGETOPT_REQ_UINT32 }
};

/** The ballooning module's payload. */
typedef struct VBOXWATCHDOG_BALLOONCTRL_PAYLOAD
{
    /** Last (most recent) ballooning size reported by the guest. */
    uint32_t cMbBalloonCurLast;
    /** Last (most recent) ballooning request received. */
    uint32_t cMbBalloonReqLast;
} VBOXWATCHDOG_BALLOONCTRL_PAYLOAD, *PVBOXWATCHDOG_BALLOONCTRL_PAYLOAD;


/*********************************************************************************************************************************
*   Globals                                                                                                                      *
*********************************************************************************************************************************/

static uint32_t g_cMsMemoryBalloonTimeout    = 30 * 1000;
static uint32_t g_cMbMemoryBalloonIncrement  = 256;
static uint32_t g_cMbMemoryBalloonDecrement  = 128;
/** Command line: Global balloon limit (in MB) for all VMs. Default is 0, which means
 *  no global limit is set. See balloonGetMaxSize() for more information. */
static uint32_t g_cMbMemoryBalloonMax        = 0;
static uint32_t g_cMbMemoryBalloonLowerLimit = 128;
static uint32_t g_cbMemoryBalloonSafety     = 1024;


/*********************************************************************************************************************************
*   Local Function Prototypes                                                                                                    *
*********************************************************************************************************************************/
static int balloonSetSize(PVBOXWATCHDOG_MACHINE pMachine, uint32_t cMbBalloonCur);

/**
 * Retrieves the current delta value
 *
 * @return  Delta (MB) of the balloon to be deflated (<0) or inflated (>0).
 * @param   pMachine            Pointer to the machine's internal structure.
 * @param   uGuestMemFree       The guest's current free memory (MB).
 * @param   cMbBalloonOld         The balloon's current (old) size (MB).
 * @param   uBalloonNew         The balloon's new size (MB).
 * @param   uBalloonMax         The maximum ballooning size (MB) it can inflate to.
 */
static int32_t balloonGetDelta(PVBOXWATCHDOG_MACHINE pMachine, uint32_t cMbGuestMemFree,
                               uint32_t cMbBalloonOld, uint32_t cMbBalloonNew, uint32_t cMbBalloonMax)
{
    serviceLogVerbose(("[%ls] cMbGuestMemFree=%RU32, cMbBalloonOld=%RU32, cMbBalloonNew=%RU32, cMbBalloonMax=%RU32\n",
                       pMachine->strName.raw(), cMbGuestMemFree, cMbBalloonOld, cMbBalloonNew, cMbBalloonMax));

    /* Make sure that the requested new ballooning size does not
     * exceed the maximum ballooning size (if set). */
    if (   cMbBalloonMax
        && cMbBalloonNew > cMbBalloonMax)
        cMbBalloonNew = cMbBalloonMax;

    int32_t cMbBalloonDelta = 0;
    if (cMbGuestMemFree < g_cMbMemoryBalloonLowerLimit)
    {
        /* Guest is running low on memory, we need to
         * deflate the balloon. */
        cMbBalloonDelta = g_cMbMemoryBalloonDecrement * -1;

        /* Ensure that the delta will not return a negative
         * balloon size. */
        if ((int32_t)cMbBalloonOld + cMbBalloonDelta < 0)
            cMbBalloonDelta = 0;
    }
    else if (cMbBalloonNew > cMbBalloonOld) /* Inflate. */
    {
        /* We want to inflate the balloon if we have room. */
        uint32_t cMbIncrement = g_cMbMemoryBalloonIncrement;
        while (   cMbIncrement >= 16
               && cMbGuestMemFree - cMbIncrement < g_cMbMemoryBalloonLowerLimit)
            cMbIncrement /= 2;

        if ((cMbGuestMemFree - cMbIncrement) > g_cMbMemoryBalloonLowerLimit)
            cMbBalloonDelta = (int32_t)cMbIncrement;

        /* Make sure we're still within bounds. */
        Assert(cMbBalloonDelta >= 0);
        if (cMbBalloonOld + cMbBalloonDelta > cMbBalloonNew)
            cMbBalloonDelta = RT_MIN(g_cMbMemoryBalloonIncrement, cMbBalloonNew - cMbBalloonOld);
    }
    else if (cMbBalloonNew < cMbBalloonOld) /* Deflate. */
    {
        cMbBalloonDelta = RT_MIN(g_cMbMemoryBalloonDecrement, cMbBalloonOld - cMbBalloonNew) * -1;
    }

    /* Limit the ballooning to the available host memory, leaving some free.
     * If anything fails clamp the delta to 0. */
    if (cMbBalloonDelta < 0)
    {
        uint64_t cbSafety = (uint64_t)g_cbMemoryBalloonSafety * _1M;
        uint64_t cbHostRamAvail = 0;
        int vrc = RTSystemQueryAvailableRam(&cbHostRamAvail);
        if (RT_SUCCESS(vrc))
        {
            if (cbHostRamAvail < cbSafety)
                cMbBalloonDelta = 0;
            else if ((uint64_t)(-cMbBalloonDelta) > (cbHostRamAvail - cbSafety) / _1M)
                cMbBalloonDelta = -(int32_t)((cbHostRamAvail - cbSafety) / _1M);
        }
        else
            cMbBalloonDelta = 0;
    }

    return cMbBalloonDelta;
}

/**
 * Determines the maximum balloon size to set for the specified machine.
 *
 * @return  Maximum ballooning size (in MB), 0 if no maximum set.
 * @param   pMachine                Machine to determine maximum ballooning size for.
 */
static uint32_t balloonGetMaxSize(PVBOXWATCHDOG_MACHINE pMachine)
{
    /*
     * Is a maximum ballooning size set? Make sure we're within bounds.
     *
     * The maximum balloning size can be set
     * - via global extra-data ("VBoxInternal/Guest/BalloonSizeMax")
     * - via command line ("--balloon-max")
     *
     * Precedence from top to bottom.
     */
    uint32_t cMbBalloonMax = 0;
    char szSource[64];

    Bstr strValue;
    HRESULT hrc = g_pVirtualBox->GetExtraData(Bstr("VBoxInternal/Guest/BalloonSizeMax").raw(),
                                              strValue.asOutParam());
    if (   SUCCEEDED(hrc)
        && strValue.isNotEmpty())
    {
        cMbBalloonMax = Utf8Str(strValue).toUInt32();
        if (g_fVerbose)
            RTStrPrintf(szSource, sizeof(szSource), "global extra-data");
    }

    if (strValue.isEmpty())
    {
        Assert(cMbBalloonMax == 0);

        cMbBalloonMax = g_cMbMemoryBalloonMax;
        if (g_fVerbose)
            RTStrPrintf(szSource, sizeof(szSource), "command line");
    }

    serviceLogVerbose(("[%ls] Maximum balloning size is (%s): %RU32MB\n", pMachine->strName.raw(), szSource, cMbBalloonMax));
    return cMbBalloonMax;
}

/**
 * Determines the current (set) balloon size of the specified machine.
 *
 * @return  IPRT status code.
 * @param   pMachine                Machine to determine maximum ballooning size for.
 * @param   pcMbBalloonCur          Where to store the current (set) balloon
 *                                  size (in MB) on success.
 */
static int balloonGetCurrentSize(PVBOXWATCHDOG_MACHINE pMachine, uint32_t *pcMbBalloonCur)
{
    LONG cKbBalloonCur;
    int vrc = getMetric(pMachine, L"Guest/RAM/Usage/Balloon", &cKbBalloonCur);
    if (RT_SUCCESS(vrc))
    {
        if (pcMbBalloonCur)
            *pcMbBalloonCur = (uint32_t)(cKbBalloonCur / 1024);
    }

    return vrc;
}

/**
 * Determines the requested balloon size to set for the specified machine.
 *
 * @return  Requested ballooning size (in MB), 0 if ballooning should be disabled.
 * @param   pMachine                Machine to determine maximum ballooning size for.
 */
static uint32_t balloonGetRequestedSize(PVBOXWATCHDOG_MACHINE pMachine)
{
    const ComPtr<IMachine> &rptrMachine = pMachine->machine;

    /*
     * The maximum balloning size can be set
     * - via per-VM extra-data ("VBoxInternal2/Watchdog/BalloonCtrl/BalloonSizeMax")
     * - via per-VM extra-data (legacy) ("VBoxInternal/Guest/BalloonSizeMax")
     *
     * Precedence from top to bottom.
     */
    uint32_t cMbBalloonReq = 0;
    char szSource[64];

    Bstr strValue;
    HRESULT hrc = rptrMachine->GetExtraData(Bstr("VBoxInternal2/Watchdog/BalloonCtrl/BalloonSizeMax").raw(),
                                            strValue.asOutParam());
    if (   SUCCEEDED(hrc)
        && strValue.isNotEmpty())
    {
        cMbBalloonReq = Utf8Str(strValue).toUInt32();
        if (g_fVerbose)
            RTStrPrintf(szSource, sizeof(szSource), "per-VM extra-data");
    }
    else
    {
        hrc = rptrMachine->GetExtraData(Bstr("VBoxInternal/Guest/BalloonSizeMax").raw(),
                                        strValue.asOutParam());
        if (   SUCCEEDED(hrc)
            && strValue.isNotEmpty())
        {
            cMbBalloonReq = Utf8Str(strValue).toUInt32();
            if (g_fVerbose)
                RTStrPrintf(szSource, sizeof(szSource), "per-VM extra-data (legacy)");
        }
    }

    if (   FAILED(hrc)
        || strValue.isEmpty())
    {
        cMbBalloonReq = 0;
        if (g_fVerbose)
            RTStrPrintf(szSource, sizeof(szSource), "none (disabled)");
    }

    serviceLogVerbose(("[%ls] Requested balloning size is (%s): %RU32MB\n", pMachine->strName.raw(), szSource, cMbBalloonReq));
    return cMbBalloonReq;
}

/**
 * Determines whether ballooning for the specified machine is enabled or not.
 * This can be specified on a per-VM basis or as a globally set value for all VMs.
 *
 * @return  bool                    Whether ballooning is enabled or not.
 * @param   pMachine                Machine to determine enable status for.
 */
static bool balloonIsEnabled(PVBOXWATCHDOG_MACHINE pMachine)
{
    const ComPtr<IMachine> &rptrMachine = pMachine->machine;

    bool fEnabled = true; /* By default ballooning is enabled. */
    char szSource[64];

    Bstr strValue;
    HRESULT hrc = g_pVirtualBox->GetExtraData(Bstr("VBoxInternal/Guest/BalloonEnabled").raw(),
                                              strValue.asOutParam());
    if (   SUCCEEDED(hrc)
        && strValue.isNotEmpty())
    {
       if (g_fVerbose)
            RTStrPrintf(szSource, sizeof(szSource), "global extra-data");
    }
    else
    {
        hrc = rptrMachine->GetExtraData(Bstr("VBoxInternal2/Watchdog/BalloonCtrl/BalloonEnabled").raw(),
                                        strValue.asOutParam());
        if (SUCCEEDED(hrc))
        {
            if (g_fVerbose)
                RTStrPrintf(szSource, sizeof(szSource), "per-VM extra-data");
        }
    }

    if (strValue.isNotEmpty())
    {
        fEnabled = RT_BOOL(Utf8Str(strValue).toUInt32());
        serviceLogVerbose(("[%ls] Ballooning is forced to %s (%s)\n",
                           pMachine->strName.raw(), fEnabled ? "enabled" : "disabled", szSource));
    }

    return fEnabled;
}

/**
 * Indicates whether ballooning on the specified machine state is
 * possible -- this only is true if the machine is up and running.
 *
 * @return  bool            Flag indicating whether the VM is running or not.
 * @param   enmState        The VM's machine state to judge whether it's running or not.
 */
static bool balloonIsPossible(MachineState_T enmState)
{
    switch (enmState)
    {
        case MachineState_Running:
#if 0
        /* Not required for ballooning. */
        case MachineState_Teleporting:
        case MachineState_LiveSnapshotting:
        case MachineState_Paused:
        case MachineState_TeleportingPausedVM:
#endif
            return true;
        default:
            break;
    }
    return false;
}

#if 0 /* unused */
static int balloonMachineSetup(const Bstr& strUuid)
{
    int vrc = VINF_SUCCESS;

    do
    {
        PVBOXWATCHDOG_MACHINE pMachine = getMachine(strUuid);
        AssertPtrBreakStmt(pMachine, vrc=VERR_INVALID_PARAMETER);

        ComPtr<IMachine> m = pMachine->machine;

        /*
         * Setup metrics required for ballooning.
         */
        com::SafeArray<BSTR> metricNames(1);
        com::SafeIfaceArray<IUnknown> metricObjects(1);
        com::SafeIfaceArray<IPerformanceMetric> metricAffected;

        Bstr strMetricNames(L"Guest/RAM/Usage");
        strMetricNames.cloneTo(&metricNames[0]);

        HRESULT hrc = m.queryInterfaceTo(&metricObjects[0]);

#ifdef VBOX_WATCHDOG_GLOBAL_PERFCOL
        CHECK_ERROR_BREAK(g_pPerfCollector, SetupMetrics(ComSafeArrayAsInParam(metricNames),
                                                         ComSafeArrayAsInParam(metricObjects),
                                                         5 /* 5 seconds */,
                                                         1 /* One sample is enough */,
                                                         ComSafeArrayAsOutParam(metricAffected)));
#else
        ComPtr<IPerformanceCollector> coll = pMachine->collector;

        CHECK_ERROR_BREAK(g_pVirtualBox, COMGETTER(PerformanceCollector)(coll.asOutParam()));
        CHECK_ERROR_BREAK(coll, SetupMetrics(ComSafeArrayAsInParam(metricNames),
                                             ComSafeArrayAsInParam(metricObjects),
                                             5 /* 5 seconds */,
                                             1 /* One sample is enough */,
                                             ComSafeArrayAsOutParam(metricAffected)));
#endif
        if (FAILED(hrc))
            vrc = VERR_COM_IPRT_ERROR; /** @todo Find better rc! */

    } while (0);

    return vrc;
}
#endif

/**
 * Does the actual ballooning and assumes the machine is
 * capable and ready for ballooning.
 *
 * @return  IPRT status code.
 * @param   pMachine                Pointer to the machine's internal structure.
 */
static int balloonMachineUpdate(PVBOXWATCHDOG_MACHINE pMachine)
{
    AssertPtrReturn(pMachine, VERR_INVALID_POINTER);

    /*
     * Get metrics collected at this point.
     */
    LONG cKbGuestMemFree;
    uint32_t cMbBalloonCur = 0;

    int vrc = getMetric(pMachine, L"Guest/RAM/Usage/Free", &cKbGuestMemFree);
    if (RT_SUCCESS(vrc))
        vrc = balloonGetCurrentSize(pMachine, &cMbBalloonCur);

    if (RT_SUCCESS(vrc))
    {
        /* If guest statistics are not up and running yet, skip this iteration and try next time. */
        if (cKbGuestMemFree <= 0)
        {
#ifdef DEBUG
            serviceLogVerbose(("[%ls] No metrics available yet!\n", pMachine->strName.raw()));
#endif
            return VINF_SUCCESS;
        }

        uint32_t cMbGuestMemFree = (ULONG)cKbGuestMemFree / 1024;

        PVBOXWATCHDOG_BALLOONCTRL_PAYLOAD pData;
        pData = (PVBOXWATCHDOG_BALLOONCTRL_PAYLOAD)payloadFrom(pMachine, VBOX_MOD_BALLOONING_NAME);
        AssertPtr(pData);

        /* Determine if ballooning is enabled or disabled. */
        bool fEnabled = balloonIsEnabled(pMachine);

        /* Determine the current set maximum balloon size. */
        uint32_t cMbBalloonMax = balloonGetMaxSize(pMachine);

        /* Determine the requested balloon size. */
        uint32_t cMbBalloonReq = balloonGetRequestedSize(pMachine);

        serviceLogVerbose(("[%ls] Free RAM (MB): %RI32, Ballooning: Current=%RU32MB, Requested=%RU32MB, Maximum=%RU32MB\n",
                           pMachine->strName.raw(), cMbGuestMemFree, cMbBalloonCur, cMbBalloonReq, cMbBalloonMax));

        if (   cMbBalloonMax
            && cMbBalloonReq > cMbBalloonMax)
        {
            if (pData->cMbBalloonReqLast != cMbBalloonReq)
                serviceLog("[%ls] Warning: Requested ballooning size (%RU32MB) exceeds set maximum ballooning size (%RU32MB), limiting ...\n",
                           pMachine->strName.raw(), cMbBalloonReq, cMbBalloonMax);
        }

        /* Calculate current balloon delta. */
        int32_t cMbBalloonDelta = balloonGetDelta(pMachine, cMbGuestMemFree, cMbBalloonCur, cMbBalloonReq, cMbBalloonMax);
#ifdef DEBUG
        serviceLogVerbose(("[%ls] cMbBalloonDelta=%RI32\n", pMachine->strName.raw(), cMbBalloonDelta));
#endif
        if (cMbBalloonDelta) /* Only do ballooning if there's really smth. to change ... */
        {
            cMbBalloonCur = cMbBalloonCur + cMbBalloonDelta;

            if (fEnabled)
            {
                serviceLog("[%ls] %s balloon by %RU32MB to %RU32MB ...\n",
                           pMachine->strName.raw(), cMbBalloonDelta > 0 ? "Inflating" : "Deflating", RT_ABS(cMbBalloonDelta), cMbBalloonCur);
                vrc = balloonSetSize(pMachine, cMbBalloonCur);
            }
            else
                serviceLogVerbose(("[%ls] Requested %s balloon by %RU32MB to %RU32MB, but ballooning is disabled\n",
                                   pMachine->strName.raw(), cMbBalloonDelta > 0 ? "inflating" : "deflating",
                                   RT_ABS(cMbBalloonDelta), cMbBalloonCur));
        }

        if (cMbBalloonCur != pData->cMbBalloonCurLast)
        {
            /* If ballooning is disabled, always bolt down the ballooning size to 0. */
            if (!fEnabled)
            {
                serviceLogVerbose(("[%ls] Ballooning is disabled, forcing to 0\n", pMachine->strName.raw()));
                int vrc2 = balloonSetSize(pMachine, 0);
                if (RT_FAILURE(vrc2))
                    serviceLog("[%ls] Error disabling ballooning, rc=%Rrc\n", pMachine->strName.raw(), vrc2);
            }
        }

        pData->cMbBalloonCurLast = cMbBalloonCur;
        pData->cMbBalloonReqLast = cMbBalloonReq;
    }
    else
        serviceLog("[%ls] Error retrieving metrics, rc=%Rrc\n", pMachine->strName.raw(), vrc);

    return vrc;
}

static int balloonSetSize(PVBOXWATCHDOG_MACHINE pMachine, uint32_t cMbBalloonCur)
{
    int vrc = VINF_SUCCESS;

    serviceLogVerbose(("[%ls] Setting balloon size to %RU32MB ...\n", pMachine->strName.raw(), cMbBalloonCur));

    if (g_fDryrun)
        return VINF_SUCCESS;

    /* Open a session for the VM. */
    HRESULT hrc;
    CHECK_ERROR_RET(pMachine->machine, LockMachine(g_pSession, LockType_Shared), VERR_ACCESS_DENIED);

    do
    {
        /* Get the associated console. */
        ComPtr<IConsole> console;
        CHECK_ERROR_BREAK(g_pSession, COMGETTER(Console)(console.asOutParam()));

        ComPtr <IGuest> guest;
        hrc = console->COMGETTER(Guest)(guest.asOutParam());
        if (SUCCEEDED(hrc))
            CHECK_ERROR_BREAK(guest, COMSETTER(MemoryBalloonSize)((LONG)cMbBalloonCur));
        else
            serviceLog("Error: Unable to set new balloon size %RU32 for machine '%ls', rc=%Rhrc\n",
                       cMbBalloonCur, pMachine->strName.raw(), hrc);
        if (FAILED(hrc))
            vrc = VERR_COM_IPRT_ERROR;

    } while (0);


    /* Unlock the machine again. */
    CHECK_ERROR_RET(g_pSession,  UnlockMachine(), VERR_ACCESS_DENIED);

    return vrc;
}

/* Callbacks. */
static DECLCALLBACK(int) VBoxModBallooningPreInit(void)
{
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) VBoxModBallooningOption(int argc, char *argv[], int *piConsumed)
{
    if (!argc) /* Take a shortcut. */
        return -1;

    AssertPtrReturn(argv, VERR_INVALID_POINTER);
    AssertPtrReturn(piConsumed, VERR_INVALID_POINTER);

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, argc, argv,
                          g_aBalloonOpts, RT_ELEMENTS(g_aBalloonOpts),
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
            case GETOPTDEF_BALLOONCTRL_BALLOONDEC:
                g_cMbMemoryBalloonDecrement = ValueUnion.u32;
                break;

            case GETOPTDEF_BALLOONCTRL_BALLOONINC:
                g_cMbMemoryBalloonIncrement = ValueUnion.u32;
                break;

            case GETOPTDEF_BALLOONCTRL_GROUPS:
                /** @todo Add ballooning groups cmd line arg. */
                break;

            case GETOPTDEF_BALLOONCTRL_BALLOONLOWERLIMIT:
                g_cMbMemoryBalloonLowerLimit = ValueUnion.u32;
                break;

            case GETOPTDEF_BALLOONCTRL_BALLOONMAX:
                g_cMbMemoryBalloonMax = ValueUnion.u32;
                break;

            case GETOPTDEF_BALLOONCTRL_BALLOONSAFETY:
                g_cbMemoryBalloonSafety = ValueUnion.u32;
                break;

            /** @todo This option is a common module option! Put
             *        this into a utility function! */
            case GETOPTDEF_BALLOONCTRL_TIMEOUTMS:
                g_cMsMemoryBalloonTimeout = ValueUnion.u32;
                if (g_cMsMemoryBalloonTimeout < 500)
                    g_cMsMemoryBalloonTimeout = 500;
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

static DECLCALLBACK(int) VBoxModBallooningInit(void)
{
    if (!g_cMsMemoryBalloonTimeout)
        cfgGetValueU32(g_pVirtualBox, NULL /* Machine */,
                       "VBoxInternal2/Watchdog/BalloonCtrl/TimeoutMS", NULL /* Per-machine */,
                       &g_cMsMemoryBalloonTimeout, 30 * 1000 /* Default is 30 seconds timeout. */);

    if (!g_cMbMemoryBalloonIncrement)
        cfgGetValueU32(g_pVirtualBox, NULL /* Machine */,
                       "VBoxInternal2/Watchdog/BalloonCtrl/BalloonIncrementMB", NULL /* Per-machine */,
                       &g_cMbMemoryBalloonIncrement, 256);

    if (!g_cMbMemoryBalloonDecrement)
        cfgGetValueU32(g_pVirtualBox, NULL /* Machine */,
                       "VBoxInternal2/Watchdog/BalloonCtrl/BalloonDecrementMB", NULL /* Per-machine */,
                       &g_cMbMemoryBalloonDecrement, 128);

    if (!g_cMbMemoryBalloonLowerLimit)
        cfgGetValueU32(g_pVirtualBox, NULL /* Machine */,
                       "VBoxInternal2/Watchdog/BalloonCtrl/BalloonLowerLimitMB", NULL /* Per-machine */,
                       &g_cMbMemoryBalloonLowerLimit, 128);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) VBoxModBallooningMain(void)
{
    static uint64_t s_msLast = UINT64_MAX;
    if (s_msLast == UINT64_MAX)
        s_msLast = RTTimeMilliTS();
    else
    {
        uint64_t msDelta = RTTimeMilliTS() - s_msLast;
        if (msDelta <= g_cMsMemoryBalloonTimeout)
            return VINF_SUCCESS;
    }

    int rc = VINF_SUCCESS;

    /** @todo Provide API for enumerating/working w/ machines inside a module! */
    mapVMIter it = g_mapVM.begin();
    while (it != g_mapVM.end())
    {
        MachineState_T state = getMachineState(&it->second);

        /* Our actual ballooning criteria. */
        if (balloonIsPossible(state))
        {
            rc = balloonMachineUpdate(&it->second);
            AssertRC(rc);
        }
        if (RT_FAILURE(rc))
            break;

        ++it;
    }

    s_msLast = RTTimeMilliTS();
    return rc;
}

static DECLCALLBACK(int) VBoxModBallooningStop(void)
{
    return VINF_SUCCESS;
}

static DECLCALLBACK(void) VBoxModBallooningTerm(void)
{
}

static DECLCALLBACK(int) VBoxModBallooningOnMachineRegistered(const Bstr &strUuid)
{
    PVBOXWATCHDOG_MACHINE pMachine = getMachine(strUuid);
    AssertPtrReturn(pMachine, VERR_INVALID_PARAMETER);

    PVBOXWATCHDOG_BALLOONCTRL_PAYLOAD pData;
    int rc = payloadAlloc(pMachine, VBOX_MOD_BALLOONING_NAME,
                          sizeof(VBOXWATCHDOG_BALLOONCTRL_PAYLOAD), (void**)&pData);
    if (RT_SUCCESS(rc))
        rc = balloonMachineUpdate(pMachine);

    return rc;
}

static DECLCALLBACK(int) VBoxModBallooningOnMachineUnregistered(const Bstr &strUuid)
{
    PVBOXWATCHDOG_MACHINE pMachine = getMachine(strUuid);
    AssertPtrReturn(pMachine, VERR_INVALID_PARAMETER);

    payloadFree(pMachine, VBOX_MOD_BALLOONING_NAME);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) VBoxModBallooningOnMachineStateChanged(const Bstr &strUuid,
                                                                MachineState_T enmState)
{
    RT_NOREF(enmState);

    PVBOXWATCHDOG_MACHINE pMachine = getMachine(strUuid);
    /* Note: The machine state will change to "setting up" when machine gets deleted,
     *       so pMachine might be NULL here. */
    if (!pMachine)
        return VINF_SUCCESS;

    return balloonMachineUpdate(pMachine);
}

static DECLCALLBACK(int) VBoxModBallooningOnServiceStateChanged(bool fAvailable)
{
    RT_NOREF(fAvailable);
    return VINF_SUCCESS;
}

/**
 * The 'balloonctrl' module description.
 */
VBOXMODULE g_ModBallooning =
{
    /* pszName. */
    VBOX_MOD_BALLOONING_NAME,
    /* pszDescription. */
    "Memory Ballooning Control",
    /* pszDepends. */
    NULL,
    /* uPriority. */
    0 /* Not used */,
    /* pszUsage. */
    "           [--balloon-dec=<MB>] [--balloon-groups=<string>]\n"
    "           [--balloon-inc=<MB>] [--balloon-interval=<ms>]\n"
    "           [--balloon-lower-limit=<MB>] [--balloon-max=<MB>]\n"
    "           [--balloon-safety-margin=<MB]\n",
    /* pszOptions. */
    "  --balloon-dec=<MB>\n"
    "      Sets the ballooning decrement in MB (128 MB).\n"
    "  --balloon-groups=<string>\n"
    "      Sets the VM groups for ballooning (all).\n"
    "  --balloon-inc=<MB>\n"
    "      Sets the ballooning increment in MB (256 MB).\n"
    "  --balloon-interval=<ms>\n"
    "      Sets the check interval in ms (30 seconds).\n"
    "  --balloon-lower-limit=<MB>\n"
    "      Sets the ballooning lower limit in MB (64 MB).\n"
    "  --balloon-max=<MB>\n"
    "      Sets the balloon maximum limit in MB (0 MB).\n"
    "      Specifying \"0\" means disabled ballooning.\n"
#if 1
    /* (Legacy) note. */
    "      Set \"VBoxInternal/Guest/BalloonSizeMax\" for a per-VM\n"
    "      maximum ballooning size.\n"
#endif
    "  --balloon-safety-margin=<MB>\n"
    "     Free memory when deflating a balloon in MB (1024 MB).\n"
    ,
    /* methods. */
    VBoxModBallooningPreInit,
    VBoxModBallooningOption,
    VBoxModBallooningInit,
    VBoxModBallooningMain,
    VBoxModBallooningStop,
    VBoxModBallooningTerm,
    /* callbacks. */
    VBoxModBallooningOnMachineRegistered,
    VBoxModBallooningOnMachineUnregistered,
    VBoxModBallooningOnMachineStateChanged,
    VBoxModBallooningOnServiceStateChanged
};

