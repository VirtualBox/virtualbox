/* $Id: wayland-helper-gtk.cpp 114774 2026-07-25 23:32:01Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Gtk helper for Wayland.
 *
 * This module implements Shared Clipboard and Drag-n-Drop
 * support for Wayland guests using Gtk library.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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

#include <iprt/file.h>
#include <iprt/localipc.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/pipe.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/ctype.h>

#include <VBox/GuestHost/DisplayServerType.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"
#include "wayland-helper-ipc.h"

#include "vbox-gtk.h"

/** Gtk session data.
 *
 * A structure which accumulates all the necessary data required to
 * maintain session between host and Wayland for clipboard sharing
 * and drag-n-drop.*/
typedef struct
{
    /* Generic VBoxClient Wayland session data (synchronization point). */
    vbcl_wl_session_t                       Base;
    /** Randomly generated session ID, should be used by
     *  both VBoxClient and vboxwl tool. */
    uint32_t                                uSessionId;
    /** IPC connection flow control between VBoxClient and vboxwl tool. */
    vbcl::ipc::data::DataIpc                *oDataIpc;
    /** IPC connection handle. */
    RTLOCALIPCSESSION                       hIpcSession;
    /** Popup window process handle. */
    RTPROCESS                               popupProc;
} vbox_wl_gtk_ipc_session_t;

/**
 * A set of objects required to handle clipboard sharing over
 * and drag-n-drop using Gtk library. */
typedef struct
{
    /** Wayland event loop thread. */
    RTTHREAD                                Thread;

    /** A flag which indicates that Wayland event loop should terminate. */
    volatile bool                           fShutdown;

    /** Communication session between host event loop and Wayland. */
    vbox_wl_gtk_ipc_session_t               Session;

    /** Pointer to the VBoxClient shared clipboard context (where this structure
     *  probably should live). */
    PSHCLCONTEXT                            pShClCtx;

    /** Local IPC server object. */
    RTLOCALIPCSERVER                        hIpcServer;

    /** IPC server socket name prefix. */
    const char                              *pcszIpcSockPrefix;
} vbox_wl_gtk_ctx_t;

/** Private data for a callback when host reports clipboard formats. */
struct vbcl_wayland_hlp_gtk_clip_hg_report_priv
{
    /** Helper context. */
    vbox_wl_gtk_ctx_t *pCtx;
    /** Clipboard formats. */
    SHCLFORMATS fFormats;
};

/** Private data for a callback when host requests clipboard
 *  data in specified format. */
struct vbcl_wayland_hlp_gtk_clip_gh_read_priv
{
    /** Helper context. */
    vbox_wl_gtk_ctx_t *pCtx;
    /** Clipboard format. */
    SHCLFORMAT uFormat;
};

/** Helper context. */
static vbox_wl_gtk_ctx_t g_GtkClipCtx;

/**
 * Start popup process.
 *
 * @returns IPRT status code.
 * @param   pSession    Session data.
 */
static int vbcl_wayland_hlp_gtk_session_popup(vbox_wl_gtk_ipc_session_t *pSession)
{
    /* Make sure valid session is in progress. */
    AssertReturn(pSession->uSessionId > 0, VERR_INVALID_PARAMETER);

    char szSessionId[64];
    RTStrPrintf(szSessionId, sizeof(szSessionId), "%u", pSession->uSessionId);

    /* Determin the vboxwl location. */
    char szVBoxWlBinary[RTPATH_MAX];
    int rc = RTPathExecDir(szVBoxWlBinary, sizeof(szVBoxWlBinary));
    AssertRCReturn(rc, rc);
    rc = RTPathAppend(szVBoxWlBinary, sizeof(szVBoxWlBinary), VBOXWL_FILENAME);
    AssertRCReturn(rc, rc);
    if (!RTFileExists(szVBoxWlBinary))
    {
        AssertCompile(sizeof(szVBoxWlBinary) > sizeof(VBOXWL_PATH));
        memcpy(szVBoxWlBinary, VBOXWL_PATH, sizeof(VBOXWL_PATH));
    }

    /* Select vboxwl action depending on session type. */
    const char *pszType = NULL;
    switch (pSession->Base.enmType)
    {
        case VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST:      pszType = VBOXWL_ARG_CLIP_HG_COPY; break;
        case VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST:   pszType = VBOXWL_ARG_CLIP_GH_ANNOUNCE; break;
        case VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST:       pszType = VBOXWL_ARG_CLIP_GH_COPY; break;
        case VBCL_WL_SESSION_TYPE_INVALID: break;
    }
    AssertReturn(pszType, VERR_INVALID_PARAMETER);

    /* Pass on the log verbosity level. */
    char szSetVerbosity[sizeof(VBOXWL_OPT_VERBOSITY) + 64];
    RTStrPrintf(szSetVerbosity, sizeof(szSetVerbosity), VBOXWL_OPT_VERBOSITY "=%u", g_cVerbosity);

    /* List of vboxwl command line arguments.*/
    const char *apszArgs[] =
    {
        szVBoxWlBinary,
        pszType,
        VBOXWL_ARG_SESSION_ID,
        szSessionId,
        szSetVerbosity,
        NULL
    };


    /* Run vboxwl in background. */
    rc = RTProcCreate(szVBoxWlBinary, apszArgs, RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH, &pSession->popupProc);
    if (RT_SUCCESS(rc))
        VBClLogVerbose(2, "started '%s' command [sid=%u]\n", szVBoxWlBinary, pSession->uSessionId);
    else
        VBClLogError("failed to start '%s' command [sid=%u]: rc=%Rrc\n", szVBoxWlBinary, pSession->uSessionId, rc);

    return rc;
}

/**
 * Prepare new session and start popup process.
 *
 * @returns IPRT status code.
 * @param   pSession        Session data.
 */
static int vbcl_wayland_hlp_gtk_session_prepare(vbox_wl_gtk_ipc_session_t *pSession)
{
    int rc;

    /* Make sure there is no leftovers from previous session. */
    Assert(pSession->uSessionId == 0);

    /* Initialize session. */
    pSession->uSessionId = RTRandU32Ex(1, 0xFFFFFFFF);

    pSession->oDataIpc = new vbcl::ipc::data::DataIpc();
    if (RT_VALID_PTR(pSession->oDataIpc))
    {
        pSession->oDataIpc->init(vbcl::ipc::FLOW_DIRECTION_SERVER,
                                 pSession->uSessionId);

        /* Start the helper tool. */
        rc = vbcl_wayland_hlp_gtk_session_popup(pSession);
        VBClLogVerbose(1, "session id=%u: started: rc=%Rrc\n", pSession->uSessionId, rc);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}

/**
 * Session callback: Generic session initializer.
 *
 * This callback starts new session.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type (unused).
 * @param   pvUser              User data.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_session_start_generic_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    VBCL_LOG_CALLBACK;

    vbox_wl_gtk_ctx_t *pCtx = (vbox_wl_gtk_ctx_t *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    RT_NOREF(enmSessionType);

    return vbcl_wayland_hlp_gtk_session_prepare(&pCtx->Session);
}

/**
 * Reset session, terminate popup process and free allocated resources.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type (unused).
 * @param   pvUser              User data.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_session_end_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    vbox_wl_gtk_ipc_session_t *pSession = (vbox_wl_gtk_ipc_session_t *)pvUser;
    AssertPtrReturn(pSession, VERR_INVALID_PARAMETER);

    RT_NOREF(enmSessionType);

    /* Make sure valid session is in progress. */
    AssertReturn(pSession->uSessionId > 0, VERR_INVALID_PARAMETER);

    int rc = RTProcWait(pSession->popupProc, RTPROCWAIT_FLAGS_BLOCK, NULL);
    if (RT_FAILURE(rc))
        rc = RTProcTerminate(pSession->popupProc);
    if (RT_FAILURE(rc))
        VBClLogError("session %u: unable to stop popup window process: rc=%Rrc\n",
                     pSession->uSessionId, rc);

    if (RT_SUCCESS(rc))
    {
        pSession->uSessionId = 0;

        pSession->oDataIpc->reset();
        delete pSession->oDataIpc;
    }

    return rc;
}

/**
 * Session callback: Handle sessions started by host events.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (IPC connection handle
 *                              to vboxwl tool).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_worker_join_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    vbox_wl_gtk_ctx_t *pCtx = (vbox_wl_gtk_ctx_t *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBCL_LOG_CALLBACK;

    /* Make sure valid session is in progress. */
    AssertReturn(pCtx->Session.uSessionId > 0, VERR_INVALID_PARAMETER);

    /* Select corresponding IPC flow depending on session type. */
    const vbcl::ipc::flow_t *pFlow;
    if      (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
        pFlow = vbcl::ipc::data::HGCopyFlow;
    else if (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
        pFlow = vbcl::ipc::data::GHAnnounceAndCopyFlow;
    else if (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
        pFlow = vbcl::ipc::data::GHCopyFlow;
    else
        AssertFailedReturn(VERR_INVALID_PARAMETER);
    AssertPtr(pFlow);

    /* Proceed with selected flow. */
    return pCtx->Session.oDataIpc->flow(pFlow, pCtx->Session.hIpcSession);
}

/**
 * @callback_method_impl{FNRTTHREAD, IPC server thread worker.}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_worker(RTTHREAD ThreadSelf, void *pvUser)
{
    vbox_wl_gtk_ctx_t * const pCtx = (vbox_wl_gtk_ctx_t *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pcszIpcSockPrefix, VERR_INVALID_POINTER);

    /*
     * Create & configure IPC server.
     */
    VBClLogVerbose(1, "starting IPC...\n");
    char szIpcServerName[128];
    int rc = vbcl_wayland_hlp_gtk_ipc_srv_name(pCtx->pcszIpcSockPrefix, szIpcServerName, sizeof(szIpcServerName));
    if (RT_SUCCESS(rc))
    {
        rc = RTLocalIpcServerCreate(&pCtx->hIpcServer, szIpcServerName, 0);
        if (RT_SUCCESS(rc))
        {
            rc = RTLocalIpcServerSetAccessMode(pCtx->hIpcServer, RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR);
            if (RT_SUCCESS(rc))
            {
                VBClLogVerbose(1, "started IPC server '%s'\n", szIpcServerName);
                RTThreadUserSignal(ThreadSelf);

                vbcl_wayland_session_init(&pCtx->Session.Base);

                /*
                 * Process IPC requests till we're told to shut down.
                 */
                while (!ASMAtomicReadBool(&pCtx->fShutdown))
                {
                    rc = RTLocalIpcServerListen(pCtx->hIpcServer, &pCtx->Session.hIpcSession);
                    if (RT_SUCCESS(rc))
                    {
                        /* Authenticate remote user. Only allow connection from
                           process who belongs to the same UID. */
                        RTUID uUid;
                        rc = RTLocalIpcSessionQueryUserId(pCtx->Session.hIpcSession, &uUid);
                        if (RT_SUCCESS(rc))
                        {
                            RTUID uLocalUID = geteuid();
                            if (   uLocalUID != 0
                                && uLocalUID == uUid)
                            {
                                VBClLogVerbose(1, "new IPC connection\n");

                                rc = VBClWaylandSessionJoinAnyType(&pCtx->Session.Base, vbcl_wayland_hlp_gtk_worker_join_cb, pCtx);

                                VBClLogVerbose(1, "IPC flow completed, rc=%Rrc\n", rc);

                                rc = vbcl_wayland_session_end(&pCtx->Session.Base,
                                                              vbcl_wayland_hlp_gtk_session_end_cb,  &pCtx->Session);
                                VBClLogVerbose(1, "IPC session ended, rc=%Rrc\n", rc);
                            }
                            else
                                VBClLogError("incoming IPC connection rejected - UID mismatch: %d/%d\n", uLocalUID, uUid);
                        }
                        else
                            VBClLogError("failed to get remote IPC UID, rc=%Rrc\n", rc);

                        RTLocalIpcSessionClose(pCtx->Session.hIpcSession);
                    }
                    else if (rc != VERR_CANCELLED)
                        VBClLogVerbose(1, "IPC connection has failed, rc=%Rrc\n", rc);
                } /* loop */

                rc = VINF_SUCCESS;
            }
            int rc2 = RTLocalIpcServerDestroy(pCtx->hIpcServer);
            AssertRCStmt(rc2, rc = RT_SUCCESS(rc) ? rc2 : rc);
        }
        else
            VBClLogError("Failed to create IPC server instance: %Rrc\n", rc);
    }
    else
        VBClLogError("Failed to assemble IPC server name: %Rrc\n", rc);
    VBClLogVerbose(1, "IPC stopped\n");
    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnProbe}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_probe(void)
{
    int fCaps = VBOX_WAYLAND_HELPER_CAP_NONE;

    if (VBGHDisplayServerTypeIsGtkAvailable())
        fCaps |= VBOX_WAYLAND_HELPER_CAP_CLIPBOARD;

    return fCaps;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnInit}
 */
RTDECL(int) vbcl_wayland_hlp_gtk_clip_init(void)
{
    VBCL_LOG_CALLBACK;

    RT_ZERO(g_GtkClipCtx);

    /* Set IPC server socket name prefix before server is started. */
    g_GtkClipCtx.pcszIpcSockPrefix = VBOXWL_SRV_NAME_PREFIX_CLIP;

    return vbcl_wayland_thread_start(&g_GtkClipCtx.Thread, vbcl_wayland_hlp_gtk_worker, "wl-gtk-ipc", &g_GtkClipCtx);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnTerm}
 */
RTDECL(int) vbcl_wayland_hlp_gtk_clip_term(void)
{
    int rc;
    int rcThread = 0;
    vbox_wl_gtk_ctx_t *pCtx = &g_GtkClipCtx;

    /* Set termination flag. */
    pCtx->fShutdown = true;

    /* Cancel IPC loop. */
    rc = RTLocalIpcServerCancel(pCtx->hIpcServer);
    if (RT_FAILURE(rc))
        VBClLogError("unable to notify IPC server about shutdown, rc=%Rrc\n", rc);

    if (RT_SUCCESS(rc))
    {
        /* Wait for Gtk event loop thread to shutdown. */
        rc = RTThreadWait(pCtx->Thread, RT_MS_30SEC, &rcThread);
        VBClLogInfo("gtk event thread exited with status, rc=%Rrc\n", rcThread);
    }
    else
        VBClLogError("unable to stop gtk thread, rc=%Rrc\n", rc);

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnSetClipboardCtx}
 */
static DECLCALLBACK(void) vbcl_wayland_hlp_gtk_clip_set_ctx(PSHCLCONTEXT pCtx)
{
    g_GtkClipCtx.pShClCtx = pCtx;
}

/**
 * Helper for vbclWaylandHlpGtkClipReadIntoCache.
 */
static bool vbclWaylandHlpGtkClipReadIntoCacheCheckCrc(void const *pvData, size_t cbData,
                                                       uint64_t u64CrcExpected, uint32_t fFormat)
{
    uint64_t const u64CrcActual = RTCrc64(pvData, cbData);
    if (u64CrcActual == u64CrcExpected)
        return true;
    VBClLogError("CRC mismatch for %#x (%#zx bytes): %#RX64, exepcted %#RX64\n", fFormat, cbData, u64CrcActual, u64CrcExpected);
    return false;
}

/**
 * Reads --clipboard-get data of the given format into the specified cache.
 *
 * The @a pszLine and @a pcbLine will be adjust (typcially contains the start or
 * all of the data).
 */
static bool vbclWaylandHlpGtkClipReadIntoCache(RTPIPE hPipeRead, char *pszLine, size_t *pcbLine, PSHCLCACHE pTmpCache,
                                               uint32_t fFormat, uint32_t cbData, uint64_t u64CrcExpected,
                                               uint64_t msStart, uint32_t cMsMax)
{
    /*
     * Allocate a cache buffer.
     */
    uint8_t *pbData;
    int rc = ShClCachePrep(pTmpCache, fFormat, cbData, (void **)&pbData);
    if (RT_FAILURE(rc))
    {
        VBClLogError("ShClCachePrep failed for %#x: %Rrc\n", fFormat, rc);
        return false;
    }

    /*
     * Copy data from pszLine into the buffer.
     */
    size_t offData = 0;
    if (*pcbLine > 0 && cbData > 0)
    {
        size_t cbToCopy = RT_MIN(cbData, *pcbLine);
        memcpy(pbData, pszLine, cbToCopy);
        if (cbToCopy >= *pcbLine)
        {
            *pcbLine = 0;
            *pszLine = '\0';
        }
        else
        {
            memmove(pszLine, &pszLine[cbToCopy], *pcbLine - cbToCopy);
            *pcbLine -= cbToCopy;
            pszLine[*pcbLine] = '\0';
        }
        offData += cbToCopy;
        if (offData >= cbData)
            return vbclWaylandHlpGtkClipReadIntoCacheCheckCrc(pbData, cbData, u64CrcExpected, fFormat);
    }

    /*
     * Read more data from the pipe.
     */
    for (;;)
    {
        size_t cbRead = 0;
        rc = RTPipeRead(hPipeRead, &pbData[offData], cbData - offData, &cbRead);
        if (RT_FAILURE(rc))
        {
            VBClLogError("RTPipeRead failed for %#x at %#zx of %#zx: %Rrc\n", fFormat, offData, cbData, rc);
            return false;
        }
        offData += cbRead;
        if (offData >= cbRead)
            return vbclWaylandHlpGtkClipReadIntoCacheCheckCrc(pbData, cbData, u64CrcExpected, fFormat);

        /* Wait for more data to become available. */
        uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
        rc = RTPipeSelectOne(hPipeRead, cMsElapsed < cMsMax ? cMsMax - cMsElapsed : RT_MS_1SEC);
        if (RT_FAILURE(rc))
        {
            VBClLogError("%s: RTPipeSelectOne failed after %RX64 ms for %#x at %#zx of %#zx: %Rrc\n",
                         __func__, RTTimeMilliTS() - msStart, fFormat, offData, cbData, rc);
            return false;
        }
    }
}

/**
 * Parses and validates a --clipboard-get data header line.
 */
static bool vbclWaylandHlpGtkClipParseOtherLine(const char *pszLine, uint32_t fFormats, uint32_t fCachedFmts,
                                                uint32_t *pfFormat, uint32_t *pcbExpected, uint64_t *pu64CrcExpected)
{
    /* Data header (/ 2nd) line: 'Format=%#x cbData=%#x crc64=%#RX64' */
    static char const s_szItem1[] = "Format=0x";
    if (memcmp(pszLine, s_szItem1, sizeof(s_szItem1) - 1) != 0)
    {
        LogRel5(("%s: fail #1\n", __func__));
        return false;
    }
    pszLine += sizeof(s_szItem1) - 1;

    *pfFormat = 0;
    char *pszNext = NULL;
    int rc = RTStrToUInt32Ex(pszLine, &pszNext, 16, pfFormat);
    if (rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
    {
        LogRel5(("%s: fail #2 - rc=%Rrc\n", __func__, rc));
        return false;
    }

    pszLine = pszNext;
    while (RT_C_IS_BLANK(*pszLine))
        pszLine++;

    static char const s_szItem2[] = "cbData=0x";
    if (memcmp(pszLine, s_szItem2, sizeof(s_szItem2) - 1) != 0)
    {
        LogRel5(("%s: fail #3\n", __func__));
        return false;
    }
    pszLine += sizeof(s_szItem2) - 1;
    rc = RTStrToUInt32Ex(pszLine, &pszNext, 16, pcbExpected);
    if (rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
    {
        LogRel5(("%s: fail #4 - %Rrc\n", __func__, rc));
        return false;
    }

    pszLine = pszNext;
    while (RT_C_IS_BLANK(*pszLine))
        pszLine++;

    static char const s_szItem3[] = "crc64=0x";
    if (memcmp(pszLine, s_szItem3, sizeof(s_szItem3) - 1) != 0)
    {
        LogRel5(("%s: fail #5\n", __func__));
        return false;
    }
    pszLine += sizeof(s_szItem3) - 1;
    rc = RTStrToUInt64Full(pszLine, 16, pu64CrcExpected);
    if (RT_FAILURE(rc))
    {
        LogRel5(("%s: fail #6 - %Rrc\n", __func__, rc));
        return false;
    }

    /*
     * Additional checks.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    if (   *pfFormat == 0
        || !RT_IS_POWER_OF_TWO(*pfFormat))
        rcExit = VBClLogError("fFormat=%#x, expected non-zero power of two value\n", *pfFormat);
    else if (!(*pfFormat & fFormats))
        rcExit = VBClLogError("fFormat=%#x is not in advertised formats (%#x)\n", *pfFormat, fFormats);
    else if (*pfFormat & fCachedFmts)
        rcExit = VBClLogError("Already read fFormat=%#x\n", *pfFormat);

#if RT_ARCH_BITS == 32
    static size_t const s_cbMax = _64M;
#else
    static size_t const s_cbMax = _512M;
#endif
    if (*pcbExpected > s_cbMax)
        rcExit = VBClLogError("cbData=%#x, max is %#zx\n", *pcbExpected, s_cbMax);

    return rcExit == RTEXITCODE_SUCCESS;
}

/**
 * Parses and validates the initial --clipboard-get line.
 */
static bool vbclWaylandHlpGtkClipParseFirstLine(const char *pszLine, uint32_t *pfFormats)
{
    /* 1st line: 'Formats=%#x' */
    static char const s_szFirstLinePrefix[] = "Formats=0x";
    if (memcmp(pszLine, s_szFirstLinePrefix, sizeof(s_szFirstLinePrefix) - 1) != 0)
    {
        LogRel5(("%s: fail #1\n", __func__));
        return false;
    }

    int rc = RTStrToUInt32Full(&pszLine[sizeof(s_szFirstLinePrefix) - 1], 16, pfFormats);
    if (RT_FAILURE(rc))
    {
        LogRel5(("%s: fail #2 - %Rrc\n", __func__, rc));
        return false;
    }

    if (*pfFormats & ~VBOX_SHCL_FMT_VALID_MASK)
    {
        VBClLogError("fFormats=%#x is not valid (valid mask %#x)\n", *pfFormats, VBOX_SHCL_FMT_VALID_MASK);
        return false;
    }

    return true;
}

/**
 * Helper that reads & parses the output from a --clipboard-get child.
 */
static RTEXITCODE vbclWaylandHlpGtkClipReadDataFromChild(RTPIPE hPipeRead, PSHCLCACHE pCache, uint32_t *pfFormats)
{
    *pfFormats = UINT32_MAX;

    char                    szLine[128];
    size_t                  offLine         = 0;
    uint32_t                fCachedFmts     = 0;
    uint64_t const          msStart         = RTTimeMilliTS();
    static uint32_t const   s_cMsMax        = RT_MS_30SEC;
    for (;;)
    {
        size_t cbRead = 0;
        int rc = RTPipeRead(hPipeRead, &szLine[offLine], sizeof(szLine) - offLine - 1, &cbRead);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_BROKEN_PIPE && offLine == 0)
                return RTEXITCODE_SUCCESS;
            return VBClLogError("%s: RTPipeRead failed: %Rrc (offLine=%#x)\n", __func__, rc, offLine);
        }

        offLine += cbRead; /* (cbRead can be zero with rc == VINF_TRY_AGAIN) */
        szLine[offLine] = '\0';

        /* Parse it if we've got full line. */
        char * const pszNewline = strchr(szLine, '\n');
        if (pszNewline)
        {
            *pszNewline = '\0';

            uint32_t   fFormat    = 0;
            uint32_t   cbData     = 0;
            uint64_t   u64Crc     = 0;
            bool const fFirstLine = *pfFormats == UINT32_MAX;
            bool const fRcParse   = fFirstLine
                                  ? vbclWaylandHlpGtkClipParseFirstLine(szLine, pfFormats)
                                  : vbclWaylandHlpGtkClipParseOtherLine(szLine, *pfFormats, fCachedFmts,
                                                                        &fFormat, &cbData, &u64Crc);
            if (!fRcParse)
                return VBClLogError("%s: bogus %s:\n%.*Rhxd\n", __func__, fFirstLine ? "1st line" : "data hdr", offLine, szLine);

            /* Shift the any content left in the line buffer to the start. */
            size_t const cbLine = &pszNewline[1] - szLine;
            memmove(szLine, &pszNewline[1], offLine - cbLine);
            offLine -= cbLine;
            szLine[offLine] = '\0';

            /* If data header, read the data info the cache. */
            if (   !fFirstLine
                && !vbclWaylandHlpGtkClipReadIntoCache(hPipeRead, szLine, &offLine, pCache,
                                                       fFormat, cbData, u64Crc, msStart, s_cMsMax))
                return RTEXITCODE_FAILURE;
        }
        /* Overflow check. */
        else if (offLine + 1 >= sizeof(szLine))
            return VBClLogError("%s: bogus output:\n%.*Rhxd\n", __func__, offLine, szLine);
        else
        {
            /* Wait for more data to become available. */
            uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
            rc = RTPipeSelectOne(hPipeRead, cMsElapsed < s_cMsMax ? s_cMsMax - cMsElapsed : RT_MS_1SEC);
            if (RT_FAILURE(rc))
                return VBClLogError("%s: RTPipeSelectOne failed after %RX64 ms: %Rrc\n", __func__, RTTimeMilliTS() - msStart, rc);
        }
    }
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnPopup}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_popup(void)
{
    PSHCLCONTEXT const pShClCtx = &g_Ctx;
    LogRel3(("%s:\n", __func__));

    /*
     * Take down the revision number before we start, so we won't can detect
     * host clipboard updates racing us.
     */
    RTCritSectEnter(&pShClCtx->Wl.CritSect);
    uint64_t const uRevision = pShClCtx->Wl.uRevision;
    RTCritSectLeave(&pShClCtx->Wl.CritSect);

    /*
     * Read the clipboard content.
     */
    /* Create pipe for reading content. */
    RTPIPE   hPipeRead  = NIL_RTPIPE;
    RTHANDLE hStdOut    = { RTHANDLETYPE_PIPE };
    int rc = RTPipeCreate(&hPipeRead, &hStdOut.u.hPipe, RTPIPE_C_INHERIT_WRITE);
    if (RT_SUCCESS(rc))
    {
        /* Pass on the log verbosity level. */
        char szSetVerbosity[sizeof(VBOXWL_OPT_VERBOSITY) + 64];
        RTStrPrintf(szSetVerbosity, sizeof(szSetVerbosity), VBOXWL_OPT_VERBOSITY "=%u", g_cVerbosity);
        const char *apszArgs[] =
        {
            RTProcExecutablePath(),
            "--clipboard-get",
            szSetVerbosity,
            NULL
        };

        /* Create the process. */
        RTPROCESS hProcess = NIL_RTPROCESS;
        rc = RTProcCreateEx(apszArgs[0], apszArgs, RTENV_DEFAULT, 0 /*fFlags*/,
                            NULL, &hStdOut, NULL, NULL, NULL, NULL, &hProcess);
        RTPipeClose(hStdOut.u.hPipe);
        if (RT_SUCCESS(rc))
        {
            /*
             * Read and process the output.
             */
            uint32_t    fFormats = UINT32_MAX;
            SHCLCACHE   Cache;
            ShClCacheInit(&Cache);
            bool const fDataOkay = vbclWaylandHlpGtkClipReadDataFromChild(hPipeRead, &Cache, &fFormats) == RTEXITCODE_SUCCESS
                                && fFormats != UINT32_MAX;
            rc = RTPipeClose(hPipeRead);
            AssertLogRelRC(rc);

            /*
             * Make sure it terminates.
             */
            RTPROCSTATUS ProcStatus = { -1, RTPROCEXITREASON_ABEND };
            uint64_t const msProcWait = RTTimeMilliTS();
            for (;;)
            {
                rc = RTProcWait(hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &ProcStatus);
                if (   (rc != VERR_PROCESS_RUNNING && rc != VERR_INTERRUPTED)
                    || RTTimeMilliTS() - msProcWait > RT_MS_1SEC)
                    break;
                RTThreadSleep(8 /*ms*/);
            }
            if (rc == VERR_PROCESS_RUNNING || rc == VERR_INTERRUPTED)
            {
                rc = RTProcTerminate(hProcess);
                rc = RTProcWait(hProcess, RT_SUCCESS(rc) ? RTPROCWAIT_FLAGS_BLOCK : RTPROCWAIT_FLAGS_NOBLOCK, &ProcStatus);
            }
            if (   RT_SUCCESS(rc)
                && ProcStatus.enmReason == RTPROCEXITREASON_NORMAL)
            {
                /*
                 * RTEXITCODE_SUCCESS: We've got data. Report it to the host.
                 */
                if (ProcStatus.iStatus == RTEXITCODE_SUCCESS && fDataOkay)
                {
                    RTCritSectEnter(&pShClCtx->Wl.CritSect);
                    if (pShClCtx->Wl.uRevision == uRevision)
                    {
                        if (   SHCLWLCTX_REV_IS_OTHER(pShClCtx->Wl.uRevision)
                            || pShClCtx->Wl.fOurFormats != fFormats
                            || !ShClCacheEquals(&pShClCtx->Wl.OurCache, &Cache))
                        {
                            LogRel2(("%s: --clipboard-get exit code: 0 - sending formats %#x and data to the host\n",
                                     __func__, fFormats));

                            VbghWaylandClipboardResetOurState(&pShClCtx->Wl, __func__, NULL);

                            pShClCtx->Wl.fOurFormats = fFormats;
                            rc = ShClCacheTransferAll(&pShClCtx->Wl.OurCache, &Cache);
                            AssertRC(rc);

                            rc = RTSemEventMultiSignal(pShClCtx->Wl.hOurCacheFilledEvent);
                            AssertRC(rc);

                            /* Do the reporting to the host. */
                            rc = VbglR3ClipboardReportFormats(pShClCtx->CmdCtx.idClient, fFormats);
                            AssertLogRelRC(rc);
                        }
                        else
                            LogRel2(("%s: --clipboard-get: skipping. data is unchanged\n", __func__));
                    }
                    else
                        LogRel2(("%s: --clipboard-get: Revision changed while getting guest clipboard data (%#RX64 -> %#RX64)\n",
                                 __func__, uRevision, pShClCtx->Wl.uRevision));
                    RTCritSectLeave(&pShClCtx->Wl.CritSect);
                }
                /*
                 * RTEXITCODE_SKIPPED: Clipboard contains host data or no data at all.
                 */
                else if (ProcStatus.iStatus == RTEXITCODE_SKIPPED && fDataOkay && fFormats == 0)
                    LogRel2(("%s: --clipboard-get exit code: skipped\n", __func__));
                /*
                 * Anything else is bad.
                 */
                else
                    VBClLogError("%s: --clipboard-get exit code: %d, fDataOkay=%d fFormats=%#x\n",
                                 __func__, ProcStatus.iStatus, fDataOkay, fFormats);
            }
            else if (RT_SUCCESS(rc) && ProcStatus.enmReason == RTPROCEXITREASON_SIGNAL)
                VBClLogError("%s: --clipboard-get terminated by signal: %d\n", __func__, ProcStatus.iStatus);
            else if (RT_SUCCESS(rc))
                VBClLogError("%s: --clipboard-get abend'ed (%d)\n", __func__, ProcStatus.iStatus);
            else
                VBClLogError("%s: RTProcWait failed on --clipboard-get: %Rrc\n", __func__, rc);
            ShClCacheTerm(&Cache);
        }
        else
        {
            VBClLogError("RTProcCreateEx failed: %Rrc\n", rc);
            RTPipeClose(hPipeRead);
        }
    }
    else
        VBClLogError("RTPipeCreate failed: %Rrc\n", rc);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Copy clipboard from the host.}
 *
 * This callback (1) sets host clipboard formats list to the session,
 * (2) waits for guest to request clipboard in specific format, (3) read
 * host clipboard in this format, and (4) sets clipboard data to the session,
 * so Gtk event thread can inject it into the guest.
 *
 * This callback should not return until clipboard data is read from
 * the host or error occurred. It must block host events loop until
 * current host event is fully processed.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_hg_report_join_cb(void *pvUser)
{
    AssertPtrReturn(pvUser, VERR_INVALID_POINTER);
    vbox_wl_gtk_ctx_t * const pCtx  = ((struct vbcl_wayland_hlp_gtk_clip_hg_report_priv const *)pvUser)->pCtx;
    SHCLFORMATS const         fFmts = ((struct vbcl_wayland_hlp_gtk_clip_hg_report_priv const *)pvUser)->fFormats;
    VBClLogVerbose(3, "%s: %#x\n", __func__, fFmts);

    pCtx->Session.oDataIpc->m_fFmts.set(fFmts);

    /** @todo r=bird: The following makes no sense as there is no guarantee that
     *        the clipboard will be read... */
    int rc;
    SHCLFORMAT const uFmt = pCtx->Session.oDataIpc->m_uFmt.wait();
    if (uFmt != pCtx->Session.oDataIpc->m_uFmt.defaults())
    {
        void    *pvData = NULL;
        uint32_t cbData = 0;
        rc = VBClClipboardReadHostClipboard(pCtx->pShClCtx, uFmt, &pvData, &cbData);
        if (RT_SUCCESS(rc))
        {
            VBClLogVerbose(5, "%s: Setting pvDataBuf=%p cbDataBuf=%#x (uFmt=%#x)...\n", __func__, pvData, cbData, uFmt);
            pCtx->Session.oDataIpc->m_pvDataBuf.set((uint64_t)pvData);
            pCtx->Session.oDataIpc->m_cbDataBuf.set((uint64_t)cbData);
        }
        else
            VBClLogError("VBClClipboardReadHostClipboard failed in %s getting %#x: %Rrc\n", __func__, uFmt, rc);
    }
    else
        rc = VERR_TIMEOUT;
    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnHGClipReport}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_hg_report(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    RT_NOREF(pCtx);
    VBClLogVerbose(3, "%s: %#x\n", __func__, fFormats);

    int rc;
    if (fFormats != VBOX_SHCL_FMT_NONE)
    {
        rc = vbcl_wayland_session_start(&g_GtkClipCtx.Session.Base,
                                        VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                        &vbcl_wayland_hlp_gtk_session_start_generic_cb,
                                        &g_GtkClipCtx);
        if (RT_SUCCESS(rc))
        {
            struct vbcl_wayland_hlp_gtk_clip_hg_report_priv Args = { &g_GtkClipCtx, fFormats };
            rc = VBClWaylandSessionJoin(&g_GtkClipCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                        vbcl_wayland_hlp_gtk_clip_hg_report_join_cb, &Args);
        }
    }
    else
    {
        /** @todo r=bird: if the current session is copy-to-copy, end it. Can we tell
         *        the rest of wayland that we no longer have any clipboard data?  */
        rc = VERR_NO_DATA;
    }

    return rc;
}


/***********************************************************************
 * DnD.
 **********************************************************************/

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_DND,pfnInit}
 */
RTDECL(int) vbcl_wayland_hlp_gtk_dnd_init(void)
{
    VBCL_LOG_CALLBACK;

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_DND,pfnTerm}
 */
RTDECL(int) vbcl_wayland_hlp_gtk_dnd_term(void)
{
    VBCL_LOG_CALLBACK;

    return VINF_SUCCESS;
}


/** GTK helper callbacks. */
const VBCLWAYLANDHELPER g_WaylandHelperGtk =
{
    /* .pszName  = */ "wayland-gtk",
    /* .pfnProbe = */ vbcl_wayland_hlp_gtk_probe,
    /* .clip     = */
    {
        /* .pfnInit            = */ vbcl_wayland_hlp_gtk_clip_init,
        /* .pfnTerm            = */ vbcl_wayland_hlp_gtk_clip_term,
        /* .pfnSetClipboardCtx = */ vbcl_wayland_hlp_gtk_clip_set_ctx,
        /* .pfnPopup           = */ vbcl_wayland_hlp_gtk_clip_popup,
        /* .pfnHGClipReport    = */ vbcl_wayland_hlp_gtk_clip_hg_report,
        /* .pfnGHClipRead      = */ NULL,
    },
    /* .dnd      = */
    {
        /* .pfnInit = */            vbcl_wayland_hlp_gtk_dnd_init,
        /* .pfnTerm = */            vbcl_wayland_hlp_gtk_dnd_term,
    }
};
