/* $Id: clipboard-common.cpp 114744 2026-07-21 18:37:21Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Shared Clipboard common code.
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

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/errcore.h>

#include "VBoxClient.h"
#include "clipboard.h"


/**
 * Read and process one event from the host clipboard service.
 *
 * @returns VBox status code.
 * @retval  VINF_CALLBACK_RETURN if a VBOX_SHCL_HOST_MSG_QUIT was received
 *          because the HGCM connection is being terminated.
 * @param   pCtx                Host Shared Clipboard service connection context.
 * @param   pfnHGClipReport     A callback to notify guest about new content in
 *                              host clipboard.
 * @param   pfnGHClipRead       A callback to notify guest when host requests
 *                              guest clipboard content.
 */
int VBClClipboardReadHostEvent(PSHCLCONTEXT pCtx, PFNHOSTCLIPREPORTFMTS pfnHGClipReport, PFNHOSTCLIPREAD pfnGHClipRead)
{
    AssertPtr(pCtx);
    AssertPtr(pfnHGClipReport);
    AssertPtr(pfnGHClipRead);

    uint32_t idMsg  = 0;
    uint32_t cParms = 0;
    int rc = VbglR3ClipboardMsgPeekWait(&pCtx->CmdCtx, &idMsg, &cParms, NULL /* pidRestoreCheck */);
    if (RT_SUCCESS(rc))
    {
        PVBGLR3CLIPBOARDEVENT pEvent = (PVBGLR3CLIPBOARDEVENT)RTMemAllocZ(sizeof(VBGLR3CLIPBOARDEVENT));
        AssertPtrReturn(pEvent, VERR_NO_MEMORY);

        rc = VbglR3ClipboardEventGetNext(idMsg, cParms, &pCtx->CmdCtx, pEvent);
        if (RT_SUCCESS(rc))
        {
            switch (pEvent->enmType)
            {
                /* Host reports new clipboard data is now available. */
                case VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS:
                    VBClLogVerbose(3, "VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS: %#x\n", pEvent->u.fReportedFormats);
                    /* Call backend worker. */
                    rc = pfnHGClipReport(pCtx, pEvent->u.fReportedFormats);
                    break;

                /* Host wants to read data from guest clipboard. */
                case VBGLR3CLIPBOARDEVENTTYPE_READ_DATA:
                    VBClLogVerbose(3, "VBGLR3CLIPBOARDEVENTTYPE_READ_DATA: %#x\n", pEvent->u.fReadData);
                    rc = pfnGHClipRead(pCtx, pEvent->u.fReadData);
                    break;

                case VBGLR3CLIPBOARDEVENTTYPE_QUIT:
                    VBClLogVerbose(2, "VBGLR3CLIPBOARDEVENTTYPE_QUIT\n");
                    rc = VINF_CALLBACK_RETURN;
                    break;

                default:
                    VBClLogVerbose(3, "VBClClipboardReadHostEvent: Unknown event: %d\n", pEvent->enmType);
                    AssertMsgFailedBreakStmt(("Event type %RU32 not implemented\n", pEvent->enmType), rc = VERR_NOT_SUPPORTED);
            }
        }
        else
            LogFlowFunc(("Getting next event failed with %Rrc\n", rc));
        VbglR3ClipboardEventFree(pEvent);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Read entire host clipboard buffer in given format.
 *
 * This function will allocate clipboard buffer of necessary size and
 * place host clipboard content into it. Buffer needs to be freed by caller.
 *
 * @returns VBox status code.
 * @param   pCtx            The VBoxClient shared clipboard context.
 * @param   uFmt            Format in which data should be read.
 * @param   ppvData         Newly allocated output buffer (should be freed by caller).
 * @param   pcbData         Output buffer size.
 */
int VBClClipboardReadHostClipboard(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData)
{
    return VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, uFmt, ppvData, pcbData);
}

