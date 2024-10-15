/* $Id: SUPSvc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Service - Common Code.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP   LOG_GROUP_SUP
#include <VBox/log.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/getopt.h>

#include "SUPSvcInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Service state.
 */
typedef enum SUPSVCSERVICESTATE
{
    kSupSvcServiceState_Invalid = 0,
    kSupSvcServiceState_NotCreated,
    kSupSvcServiceState_Paused,
    kSupSvcServiceState_Running,
    kSupSvcServiceState_End
} SUPSVCSERVICESTATE;


/**
 * Service descriptor.
 */
typedef struct SUPSVCSERVICE
{
    /** The service name. */
    const char *pszName;
    /** The service state. */
    SUPSVCSERVICESTATE enmState;
    /** The instance handle returned by pfnCreate. */
    void *pvInstance;

    /**
     * Create the service (don't start it).
     *
     * @returns VBox status code, log entry is written on failure.
     * @param   ppvInstance     Where to store the instance handle.
     */
    DECLCALLBACKMEMBER(int, pfnCreate,(void **ppvInstance));

    /**
     * Start the service.
     *
     * @param   pvInstance      The instance handle.
     */
    DECLCALLBACKMEMBER(void, pfnStart,(void *pvInstance));

    /**
     * Attempt to stop a running service.
     *
     * This should fail if there are active clients. A stopped service
     * can be restarted by calling pfnStart.
     *
     * @returns VBox status code, log entry is written on failure.
     * @param   pvInstance      The instance handle.
     */
    DECLCALLBACKMEMBER(int, pfnTryStop,(void *pvInstance));

    /**
     * Destroy the service, stopping first it if necessary.
     *
     * @param   pvInstance      The instance handle.
     * @param   fRunning        Whether the service is running or not.
     */
    DECLCALLBACKMEMBER(void, pfnStopAndDestroy,(void *pvInstance, bool fRunning));
} SUPSVCSERVICE;
/** Pointer to a service descriptor. */
typedef SUPSVCSERVICE *PSUPSVCSERVICE;
/** Pointer to a const service descriptor. */
typedef SUPSVCSERVICE const *PCSUPSVCSERVICE;



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static SUPSVCSERVICE g_aServices[] =
{
    {
        "Global",
        kSupSvcServiceState_NotCreated,
        NULL,
        supSvcGlobalCreate,
        supSvcGlobalStart,
        supSvcGlobalTryStop,
        supSvcGlobalStopAndDestroy,
    }
#ifdef RT_OS_WINDOWS
    ,
    {
        "Grant",
        kSupSvcServiceState_NotCreated,
        NULL,
        supSvcGrantCreate,
        supSvcGrantStart,
        supSvcGrantTryStop,
        supSvcGrantStopAndDestroy,
    }
#endif
};



/**
 * Instantiates and starts the services.
 *
 * @returns VBox status code. Done bitching on failure.
 */
int supSvcCreateAndStartServices(void)
{
    LogFlowFuncEnter();

    /*
     * Validate that all services are in the NotCreated state.
     */
    unsigned i;
    for (i = 0; i < RT_ELEMENTS(g_aServices); i++)
        if (g_aServices[i].enmState != kSupSvcServiceState_NotCreated)
        {
            supSvcLogError("service %s in state %d, expected state %d (NotCreated)",
                           g_aServices[i].pszName,  g_aServices[i].enmState, kSupSvcServiceState_NotCreated);
            return VERR_WRONG_ORDER;
        }

    /*
     * Create all the services, then start them.
     */
    int rc = VINF_SUCCESS;
    for (i = 0; i < RT_ELEMENTS(g_aServices); i++)
    {
        void *pvInstance = NULL;
        int rc = g_aServices[i].pfnCreate(&pvInstance);
        if (RT_FAILURE(rc))
        {
            Log(("supSvcCreateAndStartServices: %s -> %Rrc\n", g_aServices[i].pszName, rc));
            break;
        }
        g_aServices[i].pvInstance = pvInstance;
        g_aServices[i].enmState = kSupSvcServiceState_Paused;
    }
    if (RT_SUCCESS(rc))
    {
        for (i = 0; i < RT_ELEMENTS(g_aServices); i++)
        {
            g_aServices[i].pfnStart(g_aServices[i].pvInstance);
            g_aServices[i].enmState = kSupSvcServiceState_Running;
        }
    }
    else
    {
        /*
         * Destroy any services we managed to instantiate.
         */
        while (i-- > 0)
        {
            g_aServices[i].pfnStopAndDestroy(g_aServices[i].pvInstance, false /* fRunning */);
            g_aServices[i].pvInstance = NULL;
            g_aServices[i].enmState = kSupSvcServiceState_NotCreated;
        }
    }

    LogFlow(("supSvcCreateAndStartServices: returns %Rrc\n", rc));
    return rc;
}


/**
 * Checks if it's possible to stop the services.
 *
 * @returns VBox status code, done bitching on failure.
 */
int supSvcTryStopServices(void)
{
    LogFlowFuncEnter();

    /*
     * Check that the services are all created and count the running ones.
     */
    unsigned i;
    unsigned cRunning = 0;
    for (i = 0; i < RT_ELEMENTS(g_aServices); i++)
        if (g_aServices[i].enmState == kSupSvcServiceState_Running)
            cRunning++;
        else if (g_aServices[i].enmState == kSupSvcServiceState_NotCreated)
        {
            supSvcLogError("service %s in state %d (NotCreated), expected pause or running",
                           g_aServices[i].pszName,  g_aServices[i].enmState, kSupSvcServiceState_NotCreated);
            return VERR_WRONG_ORDER;
        }
    if (!cRunning)
        return VINF_SUCCESS; /* all stopped, nothing to do. */
    Assert(cRunning == RT_ELEMENTS(g_aServices)); /* all or nothing */

    /*
     * Try stop them in reverse of start order.
     */
    int rc = VINF_SUCCESS;
    i = RT_ELEMENTS(g_aServices);
    while (i-- > 0)
    {
        rc = g_aServices[i].pfnTryStop(g_aServices[i].pvInstance);
        if (RT_FAILURE(rc))
        {
            Log(("supSvcTryStopServices: %s -> %Rrc\n", g_aServices[i].pszName, rc));
            break;
        }
        g_aServices[i].enmState = kSupSvcServiceState_Paused;
    }
    if (RT_FAILURE(rc))
    {
        /* Failed, restart the ones we succeeded in stopping. */
        while (++i < RT_ELEMENTS(g_aServices))
        {
            g_aServices[i].pfnStart(g_aServices[i].pvInstance);
            g_aServices[i].enmState = kSupSvcServiceState_Running;
        }
    }
    LogFlow(("supSvcTryStopServices: returns %Rrc\n", rc));
    return rc;
}


/**
 * Stops and destroys the services.
 */
void supSvcStopAndDestroyServices(void)
{
    LogFlowFuncEnter();

    /*
     * Stop and destroy the service in reverse of start order.
     */
    unsigned i = RT_ELEMENTS(g_aServices);
    while (i-- > 0)
        if (g_aServices[i].enmState != kSupSvcServiceState_NotCreated)
        {
            g_aServices[i].pfnStopAndDestroy(g_aServices[i].pvInstance,
                                             g_aServices[i].enmState == kSupSvcServiceState_Running);
            g_aServices[i].pvInstance = NULL;
            g_aServices[i].enmState = kSupSvcServiceState_NotCreated;
        }

    LogFlowFuncLeave();
}



/**
 * Logs the message to the appropriate system log.
 *
 * In debug builds this will also put it in the debug log.
 *
 * @param   pszMsg      The log string.
 *
 * @remarks This may later be replaced by the release logger and callback destination(s).
 */
void supSvcLogErrorStr(const char *pszMsg)
{
    supSvcOsLogErrorStr(pszMsg);
    LogRel(("%s\n", pszMsg));
}


/**
 * Logs the message to the appropriate system log.
 *
 * In debug builds this will also put it in the debug log.
 *
 * @param   pszFormat   The log string. No trailing newline.
 * @param   va          Format arguments.
 *
 * @todo    This should later be replaced by the release logger and callback destination(s).
 */
void supSvcLogErrorV(const char *pszFormat, va_list va)
{
    if (*pszFormat)
    {
        char *pszMsg = NULL;
        if (RTStrAPrintfV(&pszMsg, pszFormat, va) != -1)
        {
            supSvcLogErrorStr(pszMsg);
            RTStrFree(pszMsg);
        }
        else
            supSvcLogErrorStr(pszFormat);
    }
}


/**
 * Logs the error message to the appropriate system log.
 *
 * In debug builds this will also put it in the debug log.
 *
 * @param   pszFormat   The log string. No trailing newline.
 * @param   ...         Format arguments.
 *
 * @todo    This should later be replaced by the release logger and callback destination(s).
 */
void supSvcLogError(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    supSvcLogErrorV(pszFormat, va);
    va_end(va);
}


/**
 * Deals with RTGetOpt failure, bitching in the system log.
 *
 * @returns 1
 * @param   pszAction       The action name.
 * @param   rc              The RTGetOpt return value.
 * @param   argc            The argument count.
 * @param   argv            The argument vector.
 * @param   iArg            The argument index.
 * @param   pValue          The value returned by RTGetOpt.
 */
int supSvcLogGetOptError(const char *pszAction, int rc, int argc, char **argv, int iArg, PCRTOPTIONUNION pValue)
{
    supSvcLogError("%s - RTGetOpt failure, %Rrc (%d): %s",
                   pszAction, rc, rc, iArg < argc ? argv[iArg] : "<null>");
    return 1;
}


/**
 * Bitch about too many arguments (after RTGetOpt stops) in the system log.
 *
 * @returns 1
 * @param   pszAction       The action name.
 * @param   argc            The argument count.
 * @param   argv            The argument vector.
 * @param   iArg            The argument index.
 */
int supSvcLogTooManyArgsError(const char *pszAction, int argc, char **argv, int iArg)
{
    Assert(iArg < argc);
    supSvcLogError("%s - Too many arguments: %s", pszAction, argv[iArg]);
    for ( ; iArg < argc; iArg++)
        LogRel(("arg#%i: %s\n", iArg, argv[iArg]));
    return 1;
}


/**
 * Prints an error message to the screen.
 *
 * @param   pszFormat   The message format string.
 * @param   va          Format arguments.
 */
void supSvcDisplayErrorV(const char *pszFormat, va_list va)
{
    RTStrmPrintf(g_pStdErr, "VBoxSupSvc error: ");
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    Log(("supSvcDisplayErrorV: %s", pszFormat)); /** @todo format it! */
}


/**
 * Prints an error message to the screen.
 *
 * @param   pszFormat   The message format string.
 * @param   ...         Format arguments.
 */
void supSvcDisplayError(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    supSvcDisplayErrorV(pszFormat, va);
    va_end(va);
}


/**
 * Deals with RTGetOpt failure.
 *
 * @returns 1
 * @param   pszAction       The action name.
 * @param   rc              The RTGetOpt return value.
 * @param   argc            The argument count.
 * @param   argv            The argument vector.
 * @param   iArg            The argument index.
 * @param   pValue          The value returned by RTGetOpt.
 */
int supSvcDisplayGetOptError(const char *pszAction, int rc, int argc, char **argv, int iArg, PCRTOPTIONUNION pValue)
{
    supSvcDisplayError("%s - RTGetOpt failure, %Rrc (%d): %s\n",
                       pszAction, rc, rc, iArg < argc ? argv[iArg] : "<null>");
    return 1;
}


/**
 * Bitch about too many arguments (after RTGetOpt stops).
 *
 * @returns 1
 * @param   pszAction       The action name.
 * @param   argc            The argument count.
 * @param   argv            The argument vector.
 * @param   iArg            The argument index.
 */
int supSvcDisplayTooManyArgsError(const char *pszAction, int argc, char **argv, int iArg)
{
    Assert(iArg < argc);
    supSvcDisplayError("%s - Too many arguments: %s\n", pszAction, argv[iArg]);
    return 1;
}

