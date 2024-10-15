/* $Id: display-helper-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Generic Desktop Environment helper.
 *
 * A generic helper for X11 Client which performs Desktop Environment
 * specific actions utilizing libXrandr.
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

#include "VBoxClient.h"
#include "display-helper.h"

#include <stdio.h>
#include <stdlib.h>

#include <VBox/log.h>
#include <VBox/xrandr.h>

#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/thread.h>
#include <iprt/mem.h>
#include <iprt/list.h>

/** Load libxrandr symbols needed for us. */
#include <VBox/xrandr.h>
/* Declarations of the functions that we need from libXrandr. */
#define VBOX_XRANDR_GENERATE_BODY
#include <VBox/xrandr-calls.h>

#include <X11/Xlibint.h>

/** Name of Display Change Monitor thread. */
#define VBCL_HLP_DCM_THREAD_NAME    "dcm-task"

/** Display Change Monitor thread. */
static RTTHREAD g_vbclHlpGenericDcmThread = NIL_RTTHREAD;

/** Global flag which is triggered when service requested to shutdown. */
static bool volatile g_fShutdown;

/** Node of monitors info list. */
typedef struct vbcl_hlp_generic_monitor_list_t
{
    /** List node. */
    RTLISTNODE Node;
    /** Pointer to xRandr monitor info. */
    XRRMonitorInfo *pMonitorInfo;
} vbcl_hlp_generic_monitor_list_t;

/** Pointer to display change event notification callback (set by external function call). */
static FNDISPLAYOFFSETCHANGE *g_pfnDisplayOffsetChangeCb;

/**
 * Determine monitor name strings order in a list of monitors which is sorted in ascending way.
 *
 * @return  TRUE if first name should go first in a list, FALSE otherwise.
 * @param   pszName1    First monitor name.
 * @param   pszName2    Second monitor name.
 */
static bool vbcl_hlp_generic_order_names(char *pszName1, char *pszName2)
{
    AssertReturn(pszName1, false);
    AssertReturn(pszName2, false);

    char *pszFirst = pszName1;
    char *pszSecond = pszName2;

    while (*pszFirst && *pszSecond)
    {
        if (*pszFirst < *pszSecond)
            return true;

        pszFirst++;
        pszSecond++;
    }

    return false;
}

/**
 * Insert monitor info into the list sorted ascending.
 *
 * @return  IPRT status code.
 * @param   pDisplay        X11 display handle to fetch monitor name string from.
 * @param   pListHead       Head of monitors info list.
 * @param   pMonitorInfo    Monitor info ti be inserted into the list.
 */
static int vbcl_hlp_generic_monitor_list_insert_sorted(
    Display *pDisplay, vbcl_hlp_generic_monitor_list_t *pListHead, XRRMonitorInfo *pMonitorInfo)
{
    vbcl_hlp_generic_monitor_list_t *pNode = (vbcl_hlp_generic_monitor_list_t *)RTMemAllocZ(sizeof(vbcl_hlp_generic_monitor_list_t));
    vbcl_hlp_generic_monitor_list_t *pNodeIter;
    char                            *pszMonitorName;

    AssertReturn(pNode, VERR_NO_MEMORY);

    pNode->pMonitorInfo = pMonitorInfo;

    if (RTListIsEmpty(&pListHead->Node))
    {
        RTListNodeInsertAfter(&pListHead->Node, &pNode->Node);
        return VINF_SUCCESS;
    }

    pszMonitorName = XGetAtomName(pDisplay, pMonitorInfo->name);
    AssertReturn(pszMonitorName, VERR_NO_MEMORY);

    RTListForEach(&pListHead->Node, pNodeIter, vbcl_hlp_generic_monitor_list_t, Node)
    {
        char *pszIterMonitorName = XGetAtomName(pDisplay, pNodeIter->pMonitorInfo->name);

        if (vbcl_hlp_generic_order_names(pszMonitorName, pszIterMonitorName))
        {
            RTListNodeInsertBefore(&pNodeIter->Node, &pNode->Node);
            XFree((void *)pszIterMonitorName);
            XFree((void *)pszMonitorName);
            return VINF_SUCCESS;
        }

        XFree((void *)pszIterMonitorName);
    }

    XFree((void *)pszMonitorName);

    /* If we reached the end of the list, it means that monitor
     * should be placed in the end (according to alphabetical sorting). */
    RTListNodeInsertBefore(&pNodeIter->Node, &pNode->Node);

    return VINF_SUCCESS;
}

/**
 * Release monitors info list resources.
 *
 * @param   pListHead   List head.
 */
static void vbcl_hlp_generic_free_monitor_list(vbcl_hlp_generic_monitor_list_t *pListHead)
{
    vbcl_hlp_generic_monitor_list_t *pEntry, *pNextEntry;

    RTListForEachSafe(&pListHead->Node, pEntry, pNextEntry, vbcl_hlp_generic_monitor_list_t, Node)
    {
            RTListNodeRemove(&pEntry->Node);
            RTMemFree(pEntry);
    }
}

/**
 * Handle received RRScreenChangeNotify event.
 *
 * @param   pDisplay    X11 display handle.
 */
static void vbcl_hlp_generic_process_display_change_event(Display *pDisplay)
{
    int iCount;
    uint32_t idxDisplay = 0;
    XRRMonitorInfo *pMonitorsInfo = XRRGetMonitors(pDisplay, DefaultRootWindow(pDisplay), true, &iCount);
    if (pMonitorsInfo && iCount > 0 && iCount < VBOX_DRMIPC_MONITORS_MAX)
    {
        int rc;
        vbcl_hlp_generic_monitor_list_t pMonitorsInfoList, *pIter;
        struct VBOX_DRMIPC_VMWRECT aDisplays[VBOX_DRMIPC_MONITORS_MAX];

        RTListInit(&pMonitorsInfoList.Node);

        /* Put monitors info into sorted (by monitor name) list. */
        for (int i = 0; i < iCount; i++)
        {
            rc = vbcl_hlp_generic_monitor_list_insert_sorted(pDisplay, &pMonitorsInfoList, &pMonitorsInfo[i]);
            if (RT_FAILURE(rc))
            {
                VBClLogError("unable to fill monitors info list, rc=%Rrc\n", rc);
                break;
            }
        }

        /* Now iterate over sorted list of monitor configurations. */
        RTListForEach(&pMonitorsInfoList.Node, pIter, vbcl_hlp_generic_monitor_list_t, Node)
        {
            char *pszMonitorName = XGetAtomName(pDisplay, pIter->pMonitorInfo->name);

            VBClLogVerbose(1, "reporting monitor %s offset: (%d, %d)\n",
                        pszMonitorName, pIter->pMonitorInfo->x, pIter->pMonitorInfo->y);

            XFree((void *)pszMonitorName);

            aDisplays[idxDisplay].x = pIter->pMonitorInfo->x;
            aDisplays[idxDisplay].y = pIter->pMonitorInfo->y;
            aDisplays[idxDisplay].w = pIter->pMonitorInfo->width;
            aDisplays[idxDisplay].h = pIter->pMonitorInfo->height;

            idxDisplay++;
        }

        vbcl_hlp_generic_free_monitor_list(&pMonitorsInfoList);

        XRRFreeMonitors(pMonitorsInfo);

        if (g_pfnDisplayOffsetChangeCb)
        {
            rc = g_pfnDisplayOffsetChangeCb(idxDisplay, aDisplays);
            if (RT_FAILURE(rc))
                VBClLogError("unable to notify subscriber about monitors info change, rc=%Rrc\n", rc);
        }
    }
    else
        VBClLogError("cannot get monitors info\n");
}

/** Worker thread for display change events monitoring. */
static DECLCALLBACK(int) vbcl_hlp_generic_display_change_event_monitor_worker(RTTHREAD ThreadSelf, void *pvUser)
{
    int rc = VERR_GENERAL_FAILURE;

    RT_NOREF(pvUser);

    VBClLogVerbose(1, "vbcl_hlp_generic_display_change_event_monitor_worker started\n");

    Display *pDisplay = XOpenDisplay(NULL);
    if (pDisplay)
    {
        bool fSuccess;
        int iEventBase, iErrorBase /* unused */, iMajor, iMinor;

        fSuccess  = XRRQueryExtension(pDisplay, &iEventBase, &iErrorBase);
        fSuccess &= XRRQueryVersion(pDisplay, &iMajor, &iMinor);

        if (fSuccess && iMajor >= 1 && iMinor > 3)
        {
            /* All required checks are now passed. Notify parent thread that we started. */
            RTThreadUserSignal(ThreadSelf);

            /* Only receive events we need. */
            XRRSelectInput(pDisplay, DefaultRootWindow(pDisplay), RRScreenChangeNotifyMask);

            /* Monitor main loop. */
            while (!ASMAtomicReadBool(&g_fShutdown))
            {
                XEvent Event;

                if (XPending(pDisplay) > 0)
                {
                    XNextEvent(pDisplay, &Event);
                    switch (Event.type - iEventBase)
                    {
                        case RRScreenChangeNotify:
                        {
                            vbcl_hlp_generic_process_display_change_event(pDisplay);
                            break;
                        }

                        default:
                            break;
                    }
                }
                else
                    RTThreadSleep(RT_MS_1SEC / 2);
            }
        }
        else
        {
            VBClLogError("dcm monitor cannot find XRandr 1.3+ extension\n");
            rc = VERR_NOT_AVAILABLE;
        }

        XCloseDisplay(pDisplay);
    }
    else
    {
        VBClLogError("dcm monitor cannot open X Display\n");
        rc = VERR_NOT_AVAILABLE;
    }

    VBClLogVerbose(1, "vbcl_hlp_generic_display_change_event_monitor_worker ended\n");

    return rc;
}

static void vbcl_hlp_generic_start_display_change_monitor()
{
    int rc;

    rc = RTXrandrLoadLib();
    if (RT_SUCCESS(rc))
    {
        /* Start thread which will monitor display change events. */
        rc = RTThreadCreate(&g_vbclHlpGenericDcmThread, vbcl_hlp_generic_display_change_event_monitor_worker, (void *)NULL, 0,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, VBCL_HLP_DCM_THREAD_NAME);
        if (RT_SUCCESS(rc))
        {
            rc = RTThreadUserWait(g_vbclHlpGenericDcmThread, RT_MS_5SEC);
        }
        else
            g_vbclHlpGenericDcmThread = NIL_RTTHREAD;

        VBClLogInfo("attempt to start display change monitor thread, rc=%Rrc\n", rc);

    }
    else
        VBClLogInfo("libXrandr not available, will not monitor display change events, rc=%Rrc\n", rc);
}

/**
 * @interface_method_impl{VBCLDISPLAYHELPER,pfnSetPrimaryDisplay}
 */
static DECLCALLBACK(int) vbcl_hlp_generic_set_primary_display(uint32_t idDisplay)
{
    XRRScreenResources *pScreenResources;
    Display *pDisplay;

    int rc = VERR_INVALID_PARAMETER;

    pDisplay = XOpenDisplay(NULL);
    if (pDisplay)
    {
        pScreenResources = XRRGetScreenResources(pDisplay, DefaultRootWindow(pDisplay));
        if (pScreenResources)
        {
            if ((int)idDisplay < pScreenResources->noutput)
            {
                XRRSetOutputPrimary(pDisplay, DefaultRootWindow(pDisplay), pScreenResources->outputs[idDisplay]);
                VBClLogInfo("display %u has been set as primary\n", idDisplay);
                rc = VINF_SUCCESS;
            }
            else
                VBClLogError("cannot set display %u as primary: index out of range\n", idDisplay);

            XRRFreeScreenResources(pScreenResources);
        }
        else
            VBClLogError("cannot set display %u as primary: libXrandr can not get screen resources\n", idDisplay);

        XCloseDisplay(pDisplay);
    }
    else
        VBClLogError("cannot set display %u as primary: cannot connect to X11\n", idDisplay);

    return rc;
}

/**
 * @interface_method_impl{VBCLDISPLAYHELPER,pfnProbe}
 */
static DECLCALLBACK(int) vbcl_hlp_generic_probe(void)
{
    /* Generic helper always supposed to return positive status on probe(). This
     * helper is a fallback one in case all the other helpers were failed to detect
     * their environments. */
    return VINF_SUCCESS;
}

RTDECL(int) vbcl_hlp_generic_init(void)
{
    ASMAtomicWriteBool(&g_fShutdown, false);

    /* Attempt to start display change events monitor. */
    vbcl_hlp_generic_start_display_change_monitor();

    /* Always return positive status for generic (fallback, last resort) helper. */
    return VINF_SUCCESS;
}

RTDECL(int) vbcl_hlp_generic_term(void)
{
    int rc = VINF_SUCCESS;

    if (g_vbclHlpGenericDcmThread != NIL_RTTHREAD)
    {
        /* Signal thread we are going to shutdown. */
        ASMAtomicWriteBool(&g_fShutdown, true);

        /* Wait for thread to terminate gracefully. */
        rc = RTThreadWait(g_vbclHlpGenericDcmThread, RT_MS_5SEC, NULL);
    }

    return rc;
}

RTDECL(void) vbcl_hlp_generic_subscribe_display_offset_changed(FNDISPLAYOFFSETCHANGE *pfnCb)
{
    g_pfnDisplayOffsetChangeCb = pfnCb;
}

RTDECL(void) vbcl_hlp_generic_unsubscribe_display_offset_changed(void)
{
    g_pfnDisplayOffsetChangeCb = NULL;
}

/* Helper callbacks. */
const VBCLDISPLAYHELPER g_DisplayHelperGeneric =
{
    "GENERIC",                                              /* .pszName */
    vbcl_hlp_generic_probe,                                 /* .pfnProbe */
    vbcl_hlp_generic_init,                                  /* .pfnInit */
    vbcl_hlp_generic_term,                                  /* .pfnTerm */
    vbcl_hlp_generic_set_primary_display,                   /* .pfnSetPrimaryDisplay */
    vbcl_hlp_generic_subscribe_display_offset_changed,      /* .pfnSubscribeDisplayOffsetChangeNotification */
    vbcl_hlp_generic_unsubscribe_display_offset_changed,    /* .pfnUnsubscribeDisplayOffsetChangeNotification */
};
