/* $Id: host-input-focus.cpp 114751 2026-07-22 11:12:18Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Host Input Focus Monitor.
 */

/*
 * Copyright (C) 2017-2026 Oracle and/or its affiliates.
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
#include <iprt/asm.h>
#include <iprt/thread.h>

#include <VBox/VBoxGuestLibGuestProp.h>
#include <VBox/HostServices/GuestPropertySvc.h>
#include <VBox/GuestHost/clipboard-helper.h>

#include "VBoxClient.h"


/**
 * @callback_method_impl{FNRTTHREAD,
 *      Thread procedure for the VM window input focus monitor.}
 *
 * Some Wayland helpers need to be notified about VM
 * window focus change events. This is needed in order to
 * ask about, for example, if guest clipboard content was
 * changed since last user interaction. Such guest are not
 * able to notify host about clipboard content change and
 * needed to be asked implicitly.
 */
static DECLCALLBACK(int) vbclHostInputFocusMonitorThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PVBCLHOSTINPUTFOCUSSTATE const pThis = (PVBCLHOSTINPUTFOCUSSTATE)pvUser;
    if (!pThis->pfShutdown)
        pThis->pfShutdown = &pThis->fShutdownInternal;

    int rc = VbglGuestPropConnect(&pThis->GuestPropClient);
    if (RT_SUCCESS(rc))
    {
        RTThreadUserSignal(ThreadSelf);

        /** @todo r=bird: While catching all events may be desirable, the actual
         *        callbacks may take so long to complete, that we could end up lagging
         *        too far behind events... */
        uint64_t u64PrevTimestamp = 0;  /* (try catch all events) */
        while (!ASMAtomicReadBool(pThis->pfShutdown) && !pThis->fShutdownInternal)
        {
            char  achBuf[GUEST_PROP_MAX_NAME_LEN + GUEST_PROP_MAX_VALUE_LEN + GUEST_PROP_MAX_FLAGS_LEN];
            char *pszName = NULL;
            char *pszValue = NULL;
            char *pszFlags = NULL;
            bool fWasDeleted = false;
            uint64_t u64Timestamp = 0;
            rc = VbglGuestPropWait(&pThis->GuestPropClient, VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME, achBuf, sizeof(achBuf),
                                   u64PrevTimestamp, RT_MS_30SEC, &pszName, &pszValue, &u64Timestamp, &pszFlags, NULL, &fWasDeleted);
            if (ASMAtomicReadBool(pThis->pfShutdown) && !pThis->fShutdownInternal)
                break;
            if (RT_SUCCESS(rc))
            {
                VBClLogVerbose(1, "guest property '%s' changed: '%s' flags: '%s' ts=%RU64 (+%'RU64)%s%s\n", pszName,
                               pszValue, pszFlags, u64Timestamp, u64Timestamp - u64PrevTimestamp, fWasDeleted ? " deleted" : "",
                               rc == VINF_SUCCESS ? "" : rc == VWRN_NOT_FOUND ? " VWRN_NOT_FOUND" : "VWRN_XXXX");
                u64PrevTimestamp = u64Timestamp;

                uint32_t fFlags = 0;
                if (RT_SUCCESS(GuestPropValidateFlags(pszFlags, &fFlags)))
                {
                    if (RTStrCmp(pszName, VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME) == 0)
                    {
                        if (fFlags & GUEST_PROP_F_RDONLYGUEST)
                        {
                            bool fQuit = false;
                            if (RTStrCmp(pszValue, "0") == 0)
                            {
                                if (pThis->pfnFocusExit)
                                    fQuit = pThis->pfnFocusExit(pThis);
                            }
                            else if (RTStrCmp(pszValue, "1") == 0)
                            {
                                if (pThis->pfnFocusEnter)
                                    fQuit = pThis->pfnFocusEnter(pThis);
                            }
                            else
                                VBClLogVerbose(2, "unexpected host focus property value: %s\n", pszValue);
                            if (fQuit)
                            {
                                VBClLogVerbose(1, "pfnFocus%s returned 'quit'\n", *pszValue == '0' ? "Exit" : "Enter");
                                break;
                            }
                        }
                        else
                            VBClLogError("property has invalid attributes\n");
                    }
                    else
                        VBClLogVerbose(1, "unexpected host focus property name: %s\n", pszName);
                }
                else
                    VBClLogError("guest property change: name: %s, val: %s, flags: %s, fWasDeleted: %RTbool: bad flags\n",
                                 pszName, pszValue, pszFlags, fWasDeleted);

            }
            else if (   rc != VERR_TIMEOUT
                     && rc != VERR_INTERRUPTED)
            {

                VBClLogError("VbglGuestPropWait/%s failed: %Rrc\n", VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME, rc);
                if (rc == VERR_HGCM_INVALID_CLIENT_ID)
                    break;
                RTThreadSleep(RT_MS_1SEC);
            }
        }

        VBClLogVerbose(1, "Host input focus monitor is terminating (%Rrc)\n", rc);
        VbglGuestPropDisconnect(&pThis->GuestPropClient);
    }
    else
        VBClLogError("VbglGuestPropConnect failed: %Rrc\n", rc);
    return rc;
}


/**
 * Initalizes the monitoring state to defaults, including user bits.
 */
void VBClHostInputFocusMonitorInit(PVBCLHOSTINPUTFOCUSSTATE pThis)
{
    RT_ZERO(*pThis);
    pThis->hThread = NIL_RTTHREAD;
}


/**
 * Starts the host input focus monitor thread.
 * Caller must have initialized the state.
 */
int VBClHostInputFocusMonitorStart(PVBCLHOSTINPUTFOCUSSTATE pThis, const char *pszThreadName)
{
    Assert(pThis->hThread == NIL_RTTHREAD);
    Assert(pThis->fShutdownInternal == false);
    Assert(pThis->pfnFocusEnter || pThis->pfnFocusExit);
    return VBClStartThread(&pThis->hThread, vbclHostInputFocusMonitorThread, pszThreadName, pThis);
}


/**
 * Signals that the monitor thread should stop (doesn't wait).
 */
void VBClHostInputFocusMonitorStop(PVBCLHOSTINPUTFOCUSSTATE pThis)
{
    ASMAtomicWriteBool(&pThis->fShutdownInternal, true);
    if (pThis->hThread != NIL_RTTHREAD)
        VbglGuestPropDisconnect(&pThis->GuestPropClient);
}


/**
 * Terminates the host input focus monitor thread (waits).
 *
 * Caller should first call VBClHostInputFocusMonitorStop.
 */
int VBClHostInputFocusMonitorTerm(PVBCLHOSTINPUTFOCUSSTATE pThis)
{
    int rc = VINF_SUCCESS;
    if (pThis->hThread != NIL_RTTHREAD)
    {
        /* ASSUMES VBClHostInputFocusMonitorStop is called first: */
        AssertStmt(pThis->fShutdownInternal, ASMAtomicWriteBool(&pThis->fShutdownInternal, true));

        rc = RTThreadWait(pThis->hThread, 1, NULL);
        if (rc == VERR_TIMEOUT)
        {
            RTThreadPoke(pThis->hThread);
            rc = RTThreadWait(pThis->hThread, RT_MS_30SEC, NULL);
            VBClLogVerbose(6, "VBClHostInputFocusMonitorTerm: rc=%Rrc (2nd wait)\n", rc);
        }
    }
    return rc;
}

