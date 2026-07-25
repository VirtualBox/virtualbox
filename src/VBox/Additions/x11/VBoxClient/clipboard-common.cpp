/** $Id: clipboard-common.cpp 114771 2026-07-25 21:20:37Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Shared Clipboard common code.
 */

/*
 * Copyright (C) 2007-2025 Oracle and/or its affiliates.
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


RTDECL(int) VBClClipboardReadHostEvent(PSHCLCONTEXT pCtx, const PFNHOSTCLIPREPORTFMTS pfnHGClipReport,
                                       const PFNHOSTCLIPREAD pfnGHClipRead)
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
                    rc = pfnHGClipReport(pEvent->u.fReportedFormats);
                    break;

                /* Host wants to read data from guest clipboard. */
                case VBGLR3CLIPBOARDEVENTTYPE_READ_DATA:
                    VBClLogVerbose(3, "VBGLR3CLIPBOARDEVENTTYPE_READ_DATA: %#x\n", pEvent->u.fReadData);
                    rc = pfnGHClipRead(pEvent->u.fReadData);
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

RTDECL(int) VBClClipboardReadHostClipboard(PVBGLR3SHCLCMDCTX pCtx,
                                           SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData)
{
    return VbglR3ClipboardReadDataEx(pCtx, uFmt, ppvData, pcbData);
}

