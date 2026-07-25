/** $Id: clipboard-cmds.cpp 114774 2026-07-25 23:32:01Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Clipboard commands.
 */

/*
 * Copyright (C) 2007-2026 Oracle and/or its affiliates.
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
#include "VBoxClient.h"
#include "clipboard.h"

#include <VBox/GuestHost/Wayland.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include <iprt/crc.h>
#include <iprt/pipe.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct
{
    RTEXITCODE      rcExit;
    bool volatile   fShutdown;
    RTPIPE          hPipeRead;
    RTPIPE          hPipeWrite;

    SHCLWAYLANDCTX  Wl;
} VBCLCMDCLIPBOARDSTATE;


/**
 * @interface_method_impl{SHCLWAYLANDCTX,pfnReportOurFormats}
 */
static DECLCALLBACK(void) vbclCmdClipbordGet_ReportOurFormats(PSHCLWAYLANDCTX pWlCtx, SHCLFORMATS fOurFormats)
{
    VBCLCMDCLIPBOARDSTATE *pThis = RT_FROM_MEMBER(pWlCtx, VBCLCMDCLIPBOARDSTATE, Wl);
    LogRel2(("%s: fShutdown %d -> 1; rcExit=%d -> 0\n", __func__, pThis->fShutdown, pThis->rcExit));
    pThis->fShutdown = true;
    pThis->rcExit    = RTEXITCODE_SUCCESS;

#define CHECK_RC_RETV(a_rc, a_MsgArgs) do { \
            if (RT_SUCCESS(rc)) break;\
            VBClLogError a_MsgArgs; \
            pThis->rcExit = RTEXITCODE_FAILURE; \
            return; \
        } while (0)

    /* Print the header. */
    char szBuf[128];
    size_t cch = RTStrPrintf(szBuf, sizeof(szBuf), "Formats=%#010x\n", fOurFormats);
    int rc = RTStrmWrite(g_pStdOut, szBuf, cch);
    CHECK_RC_RETV(rc, ("Initial write failed: %Rrc; rcExit=FAILURE\n", rc));

    /* Print the data for each format. */
    if (fOurFormats)
        for (uint32_t i = 0; i < RT_ELEMENTS(pThis->Wl.OurCache.aEntries); i++)
            if (RT_BIT_32(i) & fOurFormats)
            {
                cch = RTStrPrintf(szBuf, sizeof(szBuf), "Format=%#x cbData=%#x crc64=%#RX64\n",
                                  RT_BIT_32(i), pThis->Wl.OurCache.aEntries[i].cbData,
                                  RTCrc64(pThis->Wl.OurCache.aEntries[i].pvData, pThis->Wl.OurCache.aEntries[i].cbData));
                rc = RTStrmWrite(g_pStdOut, szBuf, cch);
                CHECK_RC_RETV(rc, ("Writing header for #%u failed: %Rrc; rcExit=FAILURE\n", i, rc));

                rc = RTStrmWrite(g_pStdOut, pThis->Wl.OurCache.aEntries[i].pvData, pThis->Wl.OurCache.aEntries[i].cbData);
                CHECK_RC_RETV(rc, ("Writing data for #%u failed: %Rrc; rcExit=FAILURE\n", i, rc));
            }
}


/**
 * @interface_method_impl{SHCLWAYLANDCTX,pfnDataOfferComplete}
 */
static DECLCALLBACK(void) vbclCmdClipbordGet_DataOfferComplete(PSHCLWAYLANDCTX pWlCtx)
{
    VBCLCMDCLIPBOARDSTATE *pThis = RT_FROM_MEMBER(pWlCtx, VBCLCMDCLIPBOARDSTATE, Wl);
    LogRel2(("%s: fShutdown %d -> 1; rcExit=%d\n", __func__, pThis->fShutdown, pThis->rcExit));
    pThis->fShutdown = true;

    size_t cbWritten = 0;
    int rc = RTPipeWrite(pThis->hPipeWrite, "Q", 1, &cbWritten);
    AssertLogRelMsg(RT_SUCCESS(rc) && cbWritten == 1, ("rc=%Rrc cbWritten=%zu\n", rc, cbWritten));
}


/**
 * --clipboard-get
 */
static DECLCALLBACK(RTEXITCODE) vbclCmdClipboardGet(void)
{
    VBClLogCreateNoStdOut();

    /*
     * Only wayland for now.
     */
    VBGHDISPLAYSERVERTYPE const enmDisplayServerType = VBClGetDisplayServerTypeResolveAuto();
    if (   enmDisplayServerType != VBGHDISPLAYSERVERTYPE_PURE_WAYLAND
        && enmDisplayServerType != VBGHDISPLAYSERVERTYPE_XWAYLAND)
        return VBClLogError("%s: Only available under Wayland.\n");
    int rc = VBClExplicitLoadClientLibrariesForDisplayServer(enmDisplayServerType, true /*fXWaylandAsPureWayland*/);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    /*
     * Init the wayland clipboard machinery (connects to wayland and finds the
     * necessary global objects).
     */
    VBCLCMDCLIPBOARDSTATE State = { RTEXITCODE_SKIPPED, false, NIL_RTPIPE, NIL_RTPIPE };
    RTERRINFOSTATIC       ErrInfo;
    rc = VbghWaylandClipboardInit(&State.Wl, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        VBClLogVerbose(2, "VbghWaylandClipboardInit succeeded. protocol %s\n", State.Wl.pszProtocol);
        rc = RTPipeCreate(&State.hPipeRead, &State.hPipeWrite, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            State.Wl.pfnReportOurFormats  = vbclCmdClipbordGet_ReportOurFormats;
            State.Wl.pfnDataOfferComplete = vbclCmdClipbordGet_DataOfferComplete;

            /*
             * To get the clipboard content, register the listening notifications.
             */
            rc = VbghWaylandClipboardSetupListening(&State.Wl, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(rc))
            {
                VBClLogVerbose(2, "VbghWaylandClipboardSetupListening succeeded.\n");

                /* Popup? */
                if (State.Wl.enmProtocol == SHCLWAYLANDPROTO_WL)
                {
                    rc = VbghWaylandPopupShow(&State.Wl.Popup, State.Wl.pSeatEntry, "VBoxClient-clibboard-get",
                                              "org.virtualbox.VBoxClient.clipboard.get", RTErrInfoInitStatic(&ErrInfo));
                    if (RT_FAILURE(rc))
                        VBClLogError("VbghWaylandPopupShow failed: %Rrc%#RTeim\n", rc, &ErrInfo.Core);
                }

                VbghWaylandRunloopForDisplay(State.Wl.GhCore.pDisplay, State.hPipeRead,
                                             RT_SUCCESS(rc) ? RT_MS_5SEC : RT_MS_1SEC, &State.fShutdown);

                VBClLogVerbose(2, "done (rcExit=%d).\n", State.rcExit);
                return State.rcExit;
            }
            VBClLogError("VbghWaylandClipboardSetupListening failed %Rrc%#RTeim\n", rc, &ErrInfo.Core);
        }
        VbghWaylandClipboardTerm(&State.Wl);
    }
    else
        VBClLogError("VbghWaylandClipboardInit failed %Rrc%#RTeim\n", rc, &ErrInfo.Core);
    return RTEXITCODE_FAILURE;
}

/** --clipboard-get */
VBCLCOMMAND const g_CmdClipboardGet =
{
    /* .pszName = */        "--clipboard-get",
    /* .pszDesc = */        "",
    /* .pszLogPrefix = */   "cl-get:",
    /* .pszOptions = */     NULL,
    /* .pfnOption = */      NULL,
    /* .pfnExecute = */     vbclCmdClipboardGet,
};

