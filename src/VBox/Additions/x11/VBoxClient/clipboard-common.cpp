/** $Id: clipboard-common.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Shared Clipboard common code.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

RTDECL(int) VBClClipboardThreadStart(PRTTHREAD pThread, PFNRTTHREAD pfnThread, const char *pszName, void *pvUser)
{
    int rc;

    rc = RTThreadCreate(pThread, pfnThread, pvUser, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, pszName);
    if (RT_SUCCESS(rc))
        rc = RTThreadUserWait(*pThread, RT_MS_30SEC /* msTimeout */);

    if (RT_SUCCESS(rc))
        VBClLogVerbose(1, "started %s thread\n", pszName);
    else
        LogRel(("unable to start %s thread, rc=%Rrc\n", pszName, rc));

    return rc;
}

RTDECL(int) VBClClipboardReadHostEvent(PSHCLCONTEXT pCtx, const PFNHOSTCLIPREPORTFMTS pfnHGClipReport,
                                       const PFNHOSTCLIPREAD pfnGHClipRead)
{
    int rc;

    uint32_t idMsg  = 0;
    uint32_t cParms = 0;

    AssertPtrReturn(pfnHGClipReport, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfnGHClipRead, VERR_INVALID_PARAMETER);

    PVBGLR3CLIPBOARDEVENT pEvent = (PVBGLR3CLIPBOARDEVENT)RTMemAllocZ(sizeof(VBGLR3CLIPBOARDEVENT));
    AssertPtrReturn(pEvent, VERR_NO_MEMORY);

    rc = VbglR3ClipboardMsgPeekWait(&pCtx->CmdCtx, &idMsg, &cParms, NULL /* pidRestoreCheck */);
    if (RT_SUCCESS(rc))
        rc = VbglR3ClipboardEventGetNext(idMsg, cParms, &pCtx->CmdCtx, pEvent);

    if (RT_SUCCESS(rc))
    {
        switch (pEvent->enmType)
        {
            /* Host reports new clipboard data is now available. */
            case VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS:
            {
                rc = pfnHGClipReport(pEvent->u.fReportedFormats);
                break;
            }

            /* Host wants to read data from guest clipboard. */
            case VBGLR3CLIPBOARDEVENTTYPE_READ_DATA:
            {
                rc = pfnGHClipRead(pEvent->u.fReadData);
                break;
            }

            default:
            {
                AssertMsgFailedBreakStmt(("Event type %RU32 not implemented\n", pEvent->enmType), rc = VERR_NOT_SUPPORTED);
            }
        }
    }
    else
        LogFlowFunc(("Getting next event failed with %Rrc\n", rc));

    VbglR3ClipboardEventFree(pEvent);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

RTDECL(int) VBClClipboardReadHostClipboard(PVBGLR3SHCLCMDCTX pCtx,
                                           SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData)
{
    return VbglR3ClipboardReadDataEx(pCtx, uFmt, ppvData, pcbData);
}

