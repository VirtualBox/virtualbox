/* $Id: VBoxAutostartUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxAutostart - VirtualBox Autostart service, start machines during system boot.
 *                 Utils used by the windows and posix frontends.
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

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/err.h>
#include <VBox/version.h>

#include <iprt/buildconfig.h>
#include <iprt/message.h>
#include <iprt/thread.h>
#include <iprt/stream.h>
#include <iprt/log.h>
#include <iprt/path.h>      /* RTPATH_MAX */

#include "VBoxAutostart.h"

using namespace com;

extern unsigned g_cVerbosity;

DECLHIDDEN(const char *) machineStateToName(MachineState_T machineState, bool fShort)
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
        case MachineState_Teleporting:
            return "teleporting";
        case MachineState_LiveSnapshotting:
            return fShort ? "livesnapshotting"     : "live snapshotting";
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
        case MachineState_DeletingSnapshotOnline:
            return fShort ? "deletingsnapshotlive" : "deleting snapshot live";
        case MachineState_DeletingSnapshotPaused:
            return fShort ? "deletingsnapshotlivepaused" : "deleting snapshot live paused";
        case MachineState_OnlineSnapshotting:
            return fShort ? "onlinesnapshotting"   : "online snapshotting";
        case MachineState_RestoringSnapshot:
            return fShort ? "restoringsnapshot"    : "restoring snapshot";
        case MachineState_DeletingSnapshot:
            return fShort ? "deletingsnapshot"     : "deleting snapshot";
        case MachineState_SettingUp:
            return fShort ? "settingup"           : "setting up";
        case MachineState_Snapshotting:
            return "snapshotting";
        default:
            break;
    }
    return "unknown";
}

DECLHIDDEN(void) autostartSvcShowHeader(void)
{
    RTPrintf(VBOX_PRODUCT " VirtualBox Autostart Service Version " VBOX_VERSION_STRING " - r%s\n"
             "Copyright (C) " VBOX_C_YEAR " " VBOX_VENDOR "\n\n", RTBldCfgRevisionStr());
}

DECLHIDDEN(void) autostartSvcShowVersion(bool fBrief)
{
    if (fBrief)
        RTPrintf("%s\n", VBOX_VERSION_STRING);
    else
        autostartSvcShowHeader();
}

DECLHIDDEN(int) autostartSvcLogErrorV(const char *pszFormat, va_list va)
{
    AssertPtrReturn(pszFormat, VERR_INVALID_POINTER);

    char *pszMsg = NULL;
    if (RTStrAPrintfV(&pszMsg, pszFormat, va) != -1)
    {
        autostartSvcOsLogStr(pszMsg, AUTOSTARTLOGTYPE_ERROR);
        RTStrFree(pszMsg);
        return VINF_SUCCESS;
    }

    return VERR_BUFFER_OVERFLOW;
}

DECLHIDDEN(int) autostartSvcLogError(const char *pszFormat, ...)
{
    AssertPtrReturn(pszFormat, VERR_INVALID_POINTER);

    va_list va;
    va_start(va, pszFormat);
    int rc = autostartSvcLogErrorV(pszFormat, va);
    va_end(va);

    return rc;
}

DECLHIDDEN(int) autostartSvcLogErrorRcV(int rc, const char *pszFormat, va_list va)
{
    AssertPtrReturn(pszFormat, VERR_INVALID_POINTER);

    int rc2 = autostartSvcLogErrorV(pszFormat, va);
    if (RT_SUCCESS(rc2))
        return rc; /* Return handed-in rc. */
    return rc2;
}

DECLHIDDEN(int) autostartSvcLogErrorRc(int rc, const char *pszFormat, ...)
{
    AssertPtrReturn(pszFormat, VERR_INVALID_POINTER);

    va_list va;
    va_start(va, pszFormat);
    int rc2 = autostartSvcLogErrorRcV(rc, pszFormat, va);
    va_end(va);
    return rc2;
}

DECLHIDDEN(void) autostartSvcLogVerboseV(unsigned cVerbosity, const char *pszFormat, va_list va)
{
    AssertPtrReturnVoid(pszFormat);

    if (g_cVerbosity < cVerbosity)
       return;

    char *pszMsg = NULL;
    if (RTStrAPrintfV(&pszMsg, pszFormat, va) != -1)
    {
        autostartSvcOsLogStr(pszMsg, AUTOSTARTLOGTYPE_VERBOSE);
        RTStrFree(pszMsg);
    }
}

DECLHIDDEN(void) autostartSvcLogVerbose(unsigned cVerbosity, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    autostartSvcLogVerboseV(cVerbosity, pszFormat, va);
    va_end(va);
}

DECLHIDDEN(void) autostartSvcLogWarningV(const char *pszFormat, va_list va)
{
    AssertPtrReturnVoid(pszFormat);

    char *pszMsg = NULL;
    if (RTStrAPrintfV(&pszMsg, pszFormat, va) != -1)
    {
        autostartSvcOsLogStr(pszMsg, AUTOSTARTLOGTYPE_WARNING);
        RTStrFree(pszMsg);
    }
}

DECLHIDDEN(void) autostartSvcLogWarning(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    autostartSvcLogWarningV(pszFormat, va);
    va_end(va);
}

DECLHIDDEN(void) autostartSvcLogInfo(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    autostartSvcLogInfoV(pszFormat, va);
    va_end(va);
}

DECLHIDDEN(void) autostartSvcLogInfoV(const char *pszFormat, va_list va)
{
    AssertPtrReturnVoid(pszFormat);

    char *pszMsg = NULL;
    if (RTStrAPrintfV(&pszMsg, pszFormat, va) != -1)
    {
        autostartSvcOsLogStr(pszMsg, AUTOSTARTLOGTYPE_INFO);
        RTStrFree(pszMsg);
    }
}

DECLHIDDEN(int) autostartSvcLogGetOptError(const char *pszAction, int rc, int argc, char **argv, int iArg, PCRTGETOPTUNION pValue)
{
    RT_NOREF(pValue);
    autostartSvcLogError("%s - RTGetOpt failure, %Rrc (%d): %s\n", pszAction, rc, rc, iArg < argc ? argv[iArg] : "<null>");
    return RTEXITCODE_SYNTAX;
}

DECLHIDDEN(int) autostartSvcLogTooManyArgsError(const char *pszAction, int argc, char **argv, int iArg)
{
    AssertReturn(iArg < argc, RTEXITCODE_FAILURE);
    autostartSvcLogError("%s - Too many arguments: %s\n", pszAction, argv[iArg]);
    for ( ; iArg < argc; iArg++)
        LogRel(("arg#%i: %s\n", iArg, argv[iArg]));
    return VERR_INVALID_PARAMETER;
}

DECLHIDDEN(RTEXITCODE) autostartSvcDisplayErrorV(const char *pszFormat, va_list va)
{
    RTStrmPrintf(g_pStdErr, "Error: ");
    RTStrmPrintfV(g_pStdErr, pszFormat, va);
    Log(("autostartSvcDisplayErrorV: %s\n", pszFormat)); /** @todo format it! */
    return RTEXITCODE_FAILURE;
}

DECLHIDDEN(RTEXITCODE) autostartSvcDisplayError(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    autostartSvcDisplayErrorV(pszFormat, va);
    va_end(va);
    return RTEXITCODE_FAILURE;
}

DECLHIDDEN(RTEXITCODE) autostartSvcDisplayGetOptError(const char *pszAction, int rc, PCRTGETOPTUNION pValue)
{
    char szMsg[4096];
    RTGetOptFormatError(szMsg, sizeof(szMsg), rc, pValue);
    autostartSvcDisplayError("%s - %s\n", pszAction, szMsg);
    return RTEXITCODE_SYNTAX;
}

DECLHIDDEN(int) autostartSetup(void)
{
    autostartSvcOsLogStr("Setting up ...\n", AUTOSTARTLOGTYPE_VERBOSE);

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
        autostartSvcLogError("Failed to initialize COM because the global settings directory '%s' is not accessible!", szHome);
        return VERR_COM_FILE_ERROR;
    }
# endif
    if (FAILED(hrc))
    {
        autostartSvcLogError("Failed to initialize COM (%Rhrc)!", hrc);
        return VERR_COM_UNEXPECTED;
    }

    hrc = g_pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
    if (FAILED(hrc))
    {
        RTMsgError("Failed to create the VirtualBoxClient object (%Rhrc)!", hrc);
        com::ErrorInfo info;
        if (!info.isFullAvailable() && !info.isBasicAvailable())
        {
            com::GluePrintRCMessage(hrc);
            autostartSvcLogError("Most likely, the VirtualBox COM server is not running or failed to start.");
        }
        else
            com::GluePrintErrorInfo(info);
        return VERR_COM_UNEXPECTED;
    }

    /*
     * Setup VirtualBox + session interfaces.
     */
    hrc = g_pVirtualBoxClient->COMGETTER(VirtualBox)(g_pVirtualBox.asOutParam());
    if (SUCCEEDED(hrc))
    {
        hrc = g_pSession.createInprocObject(CLSID_Session);
        if (FAILED(hrc))
            autostartSvcLogError("Failed to create a session object (rc=%Rhrc)!", hrc);
    }
    else
        autostartSvcLogError("Failed to get VirtualBox object (rc=%Rhrc)!", hrc);

    if (FAILED(hrc))
        return VERR_COM_OBJECT_NOT_FOUND;

    return VINF_SUCCESS;
}

DECLHIDDEN(void) autostartShutdown(void)
{
    autostartSvcOsLogStr("Shutting down ...\n", AUTOSTARTLOGTYPE_VERBOSE);

    g_pSession.setNull();
    g_pVirtualBox.setNull();
    g_pVirtualBoxClient.setNull();
    com::Shutdown();
}

