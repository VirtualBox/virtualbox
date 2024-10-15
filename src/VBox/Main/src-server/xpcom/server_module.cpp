/* $Id: server_module.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * XPCOM server process helper module implementation functions
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

#define LOG_GROUP LOG_GROUP_MAIN_VBOXSVC
#include <nsMemory.h>
#include <nsString.h>
#include <nsCOMPtr.h>
#include <nsIFile.h>
#include <nsIGenericFactory.h>
#include <nsIServiceManagerUtils.h>
#include <nsICategoryManager.h>
#include <nsDirectoryServiceDefs.h>

#include <ipcIService.h>
#include <ipcIDConnectService.h>
#include <ipcCID.h>
#include <ipcdclient.h>

// official XPCOM headers don't define it yet
#define IPC_DCONNECTSERVICE_CONTRACTID \
    "@mozilla.org/ipc/dconnect-service;1"

// generated file
#include <VBox/com/VirtualBox.h>

#include "server.h"
#include "LoggingNew.h"

#include <iprt/errcore.h>

#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/process.h>
#include <iprt/env.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#if defined(RT_OS_SOLARIS)
# include <sys/systeminfo.h>
#endif

/// @todo move this to RT headers (and use them in MachineImpl.cpp as well)
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
#define HOSTSUFF_EXE ".exe"
#else /* !RT_OS_WINDOWS */
#define HOSTSUFF_EXE ""
#endif /* !RT_OS_WINDOWS */


/** Name of the server executable. */
static const char g_szVBoxSVC_exe[] = "VBoxSVC" HOSTSUFF_EXE;

enum
{
    /** Amount of time to wait for the server to establish a connection, ms */
    VBoxSVC_Timeout = 30000,
    /** How often to perform a connection check, ms */
    VBoxSVC_WaitSlice = 100
};

/**
 *  Full path to the VBoxSVC executable.
 */
static char g_szVBoxSVCPath[RTPATH_MAX];
static bool g_fIsVBoxSVCPathSet = false;

/*
 *  The following macros define the method necessary to provide a list of
 *  interfaces implemented by the VirtualBox component. Note that this must be
 *  in sync with macros used for VirtualBox in server.cpp for the same purpose.
 */

NS_DECL_CLASSINFO(VirtualBoxWrap)
NS_IMPL_CI_INTERFACE_GETTER1(VirtualBoxWrap, IVirtualBox)

static nsresult vboxsvcSpawnDaemon(void)
{
    /*
     * Setup an anonymous pipe that we can use to determine when the daemon
     * process has started up.  the daemon will write a char to the pipe, and
     * when we read it, we'll know to proceed with trying to connect to the
     * daemon.
     */
    RTPIPE hPipeWr = NIL_RTPIPE;
    RTPIPE hPipeRd = NIL_RTPIPE;
    int vrc = RTPipeCreate(&hPipeRd, &hPipeWr, RTPIPE_C_INHERIT_WRITE);
    if (RT_SUCCESS(vrc))
    {
        char szPipeInheritFd[32]; RT_ZERO(szPipeInheritFd);
        const char *apszArgs[] =
        {
            g_szVBoxSVCPath,
            "--auto-shutdown",
            "--inherit-startup-pipe",
            &szPipeInheritFd[0],
            NULL
        };

        ssize_t cch = RTStrFormatU32(&szPipeInheritFd[0], sizeof(szPipeInheritFd),
                                     (uint32_t)RTPipeToNative(hPipeWr), 10 /*uiBase*/,
                                     0 /*cchWidth*/, 0 /*cchPrecision*/, 0 /*fFlags*/);
        Assert(cch > 0); RT_NOREF(cch);

        RTHANDLE hStdNil;
        hStdNil.enmType = RTHANDLETYPE_FILE;
        hStdNil.u.hFile = NIL_RTFILE;

        vrc = RTProcCreateEx(g_szVBoxSVCPath, apszArgs, RTENV_DEFAULT,
                             RTPROC_FLAGS_DETACHED, &hStdNil, &hStdNil, &hStdNil,
                             NULL /* pszAsUser */, NULL /* pszPassword */, NULL /* pExtraData */,
                             NULL /* phProcess */);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTPipeClose(hPipeWr); AssertRC(vrc); RT_NOREF(vrc);
            hPipeWr = NIL_RTPIPE;

            size_t cbRead = 0;
            char msg[10];
            memset(msg, '\0', sizeof(msg));
            vrc = RTPipeReadBlocking(hPipeRd, &msg[0], sizeof(msg) - 1, &cbRead);
            if (   RT_SUCCESS(vrc)
                && cbRead == 5
                && !strcmp(msg, "READY"))
            {
                RTPipeClose(hPipeRd);
                return NS_OK;
            }
        }

        if (hPipeWr != NIL_RTPIPE)
            RTPipeClose(hPipeWr);
        RTPipeClose(hPipeRd);
    }

    return NS_ERROR_FAILURE;
}


/**
 *  VirtualBox component constructor.
 *
 *  This constructor is responsible for starting the VirtualBox server
 *  process, connecting to it, and redirecting the constructor request to the
 *  VirtualBox component defined on the server.
 */
static NS_IMETHODIMP
VirtualBoxConstructor(nsISupports *aOuter, REFNSIID aIID,
                      void **aResult)
{
    LogFlowFuncEnter();

    nsresult rc = NS_OK;

    do
    {
        *aResult = NULL;
        if (NULL != aOuter)
        {
            rc = NS_ERROR_NO_AGGREGATION;
            break;
        }

        if (!g_fIsVBoxSVCPathSet)
        {
            /* Get the directory containing XPCOM components -- the VBoxSVC
             * executable is expected in the parent directory. */
            nsCOMPtr<nsIProperties> dirServ = do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rc);
            if (NS_SUCCEEDED(rc))
            {
                nsCOMPtr<nsIFile> componentDir;
                rc = dirServ->Get(NS_XPCOM_COMPONENT_DIR,
                                  NS_GET_IID(nsIFile), getter_AddRefs(componentDir));

                if (NS_SUCCEEDED(rc))
                {
                    nsCAutoString path;
                    componentDir->GetNativePath(path);

                    LogFlowFunc(("component directory = \"%s\"\n", path.get()));

#if defined(RT_OS_SOLARIS) && defined(VBOX_WITH_HARDENING)
                    char szKernArch[128];
                    int  cbKernArch = sysinfo(SI_ARCHITECTURE_K, szKernArch, sizeof(szKernArch));
                    AssertBreakStmt(cbKernArch > 0, rc = NS_ERROR_UNEXPECTED);
# ifdef VBOX_PATH_APP_PRIVATE
                    sprintf(g_szVBoxSVCPath, VBOX_PATH_APP_PRIVATE "/%s/%s", szKernArch, g_szVBoxSVC_exe);
# else
                    sprintf(g_szVBoxSVCPath, "/opt/VirtualBox/%s/%s", szKernArch, g_szVBoxSVC_exe);
# endif
                    g_fIsVBoxSVCPathSet = true;
#else
                    int vrc = RTStrCopy(g_szVBoxSVCPath, sizeof(g_szVBoxSVCPath), path.get());
                    AssertRCBreakStmt(vrc, rc = NS_ERROR_FAILURE);

                    RTPathStripFilename(g_szVBoxSVCPath); /* ".." to the parent directory */

                    vrc = RTPathAppend(g_szVBoxSVCPath, sizeof(g_szVBoxSVCPath), g_szVBoxSVC_exe);
                    AssertRCBreakStmt(vrc, rc = NS_ERROR_FAILURE);
                    g_fIsVBoxSVCPathSet = true;
#endif
                }
            }
            if (NS_FAILED(rc))
                break;
        }

        nsCOMPtr<ipcIService> ipcServ = do_GetService(IPC_SERVICE_CONTRACTID, &rc);
        if (NS_FAILED(rc))
            break;

        /* connect to the VBoxSVC server process */

        bool startedOnce = false;
        unsigned timeLeft = VBoxSVC_Timeout;

        do
        {
            LogFlowFunc(("Resolving server name \"%s\"...\n", VBOXSVC_IPC_NAME));

            PRUint32 serverID = 0;
            rc = ipcServ->ResolveClientName(VBOXSVC_IPC_NAME, &serverID);
            if (NS_FAILED(rc))
            {
                LogFlowFunc(("Starting server \"%s\"...\n", g_szVBoxSVCPath));

                startedOnce = true;

                rc = vboxsvcSpawnDaemon();
                if (NS_FAILED(rc))
                    break;

                /* wait for the server process to establish a connection */
                do
                {
                    RTThreadSleep(VBoxSVC_WaitSlice);
                    rc = ipcServ->ResolveClientName(VBOXSVC_IPC_NAME, &serverID);
                    if (NS_SUCCEEDED(rc))
                        break;
                    if (timeLeft <= VBoxSVC_WaitSlice)
                    {
                        timeLeft = 0;
                        break;
                    }
                    timeLeft -= VBoxSVC_WaitSlice;
                }
                while (1);

                if (!timeLeft)
                {
                    rc = IPC_ERROR_WOULD_BLOCK;
                    break;
                }
            }

            LogFlowFunc(("Connecting to server (ID=%d)...\n", serverID));

            nsCOMPtr<ipcIDConnectService> dconServ =
                do_GetService(IPC_DCONNECTSERVICE_CONTRACTID, &rc);
            if (NS_FAILED(rc))
                break;

            rc = dconServ->CreateInstance(serverID,
                                          CLSID_VirtualBox,
                                          aIID, aResult);
            if (NS_SUCCEEDED(rc))
                break;

            LogFlowFunc(("Failed to connect (rc=%Rhrc (%#08x))\n", rc, rc));

            /* It's possible that the server gets shut down after we
             * successfully resolve the server name but before it
             * receives our CreateInstance() request. So, check for the
             * name again, and restart the cycle if it fails. */
            if (!startedOnce)
            {
                nsresult rc2 =
                    ipcServ->ResolveClientName(VBOXSVC_IPC_NAME, &serverID);
                if (NS_SUCCEEDED(rc2))
                    break;

                LogFlowFunc(("Server seems to have terminated before receiving our request. Will try again.\n"));
            }
            else
                break;
        }
        while (1);
    }
    while (0);

    LogFlowFunc(("rc=%Rhrc (%#08x)\n", rc, rc));
    LogFlowFuncLeave();

    return rc;
}

#if 0
/// @todo not really necessary for the moment
/**
 *
 * @param aCompMgr
 * @param aPath
 * @param aLoaderStr
 * @param aType
 * @param aInfo
 *
 * @return
 */
static NS_IMETHODIMP
VirtualBoxRegistration(nsIComponentManager *aCompMgr,
                       nsIFile *aPath,
                       const char *aLoaderStr,
                       const char *aType,
                       const nsModuleComponentInfo *aInfo)
{
    nsCAutoString modulePath;
    aPath->GetNativePath(modulePath);
    nsCAutoString moduleTarget;
    aPath->GetNativeTarget(moduleTarget);

    LogFlowFunc(("aPath=%s, aTarget=%s, aLoaderStr=%s, aType=%s\n",
                 modulePath.get(), moduleTarget.get(), aLoaderStr, aType));

    nsresult rc = NS_OK;

    return rc;
}
#endif

/**
 *  Component definition table.
 *  Lists all components defined in this module.
 */
static const nsModuleComponentInfo components[] =
{
    {
        "VirtualBox component", // description
        NS_VIRTUALBOX_CID, NS_VIRTUALBOX_CONTRACTID, // CID/ContractID
        VirtualBoxConstructor, // constructor function
        NULL, /* VirtualBoxRegistration, */ // registration function
        NULL, // deregistration function
        NULL, // destructor function
        /// @todo
        NS_CI_INTERFACE_GETTER_NAME(VirtualBoxWrap), // interfaces function
        NULL, // language helper
        /// @todo
        &NS_CLASSINFO_NAME(VirtualBoxWrap) // global class info & flags
    }
};

NS_IMPL_NSGETMODULE(VirtualBox_Server_Module, components)
