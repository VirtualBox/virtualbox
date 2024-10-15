/* $Id: DnDUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD - Common utility functions.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#include <VBox/GuestHost/DragAndDrop.h>
#include <VBox/HostServices/DragAndDropSvc.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>

using namespace DragAndDropSvc;

/**
 * Converts a host HGCM message to a string.
 *
 * @returns Stringified version of the host message.
 */
const char *DnDHostMsgToStr(uint32_t uMsg)
{
    switch (uMsg)
    {
        RT_CASE_RET_STR(HOST_DND_FN_SET_MODE);
        RT_CASE_RET_STR(HOST_DND_FN_CANCEL);
        RT_CASE_RET_STR(HOST_DND_FN_HG_EVT_ENTER);
        RT_CASE_RET_STR(HOST_DND_FN_HG_EVT_MOVE);
        RT_CASE_RET_STR(HOST_DND_FN_HG_EVT_LEAVE);
        RT_CASE_RET_STR(HOST_DND_FN_HG_EVT_DROPPED);
        RT_CASE_RET_STR(HOST_DND_FN_HG_SND_DATA_HDR);
        RT_CASE_RET_STR(HOST_DND_FN_HG_SND_DATA);
        RT_CASE_RET_STR(HOST_DND_FN_HG_SND_MORE_DATA);
        RT_CASE_RET_STR(HOST_DND_FN_HG_SND_DIR);
        RT_CASE_RET_STR(HOST_DND_FN_HG_SND_FILE_DATA);
        RT_CASE_RET_STR(HOST_DND_FN_HG_SND_FILE_HDR);
        RT_CASE_RET_STR(HOST_DND_FN_GH_REQ_PENDING);
        RT_CASE_RET_STR(HOST_DND_FN_GH_EVT_DROPPED);
        default:
            break;
    }
    return "unknown";
}

/**
 * Converts a guest HGCM message to a string.
 *
 * @returns Stringified version of the guest message.
 */
const char *DnDGuestMsgToStr(uint32_t uMsg)
{
    switch (uMsg)
    {
        RT_CASE_RET_STR(GUEST_DND_FN_CONNECT);
        RT_CASE_RET_STR(GUEST_DND_FN_DISCONNECT);
        RT_CASE_RET_STR(GUEST_DND_FN_REPORT_FEATURES);
        RT_CASE_RET_STR(GUEST_DND_FN_QUERY_FEATURES);
        RT_CASE_RET_STR(GUEST_DND_FN_GET_NEXT_HOST_MSG);
        RT_CASE_RET_STR(GUEST_DND_FN_EVT_ERROR);
        RT_CASE_RET_STR(GUEST_DND_FN_HG_ACK_OP);
        RT_CASE_RET_STR(GUEST_DND_FN_HG_REQ_DATA);
        RT_CASE_RET_STR(GUEST_DND_FN_HG_EVT_PROGRESS);
        RT_CASE_RET_STR(GUEST_DND_FN_GH_ACK_PENDING);
        RT_CASE_RET_STR(GUEST_DND_FN_GH_SND_DATA_HDR);
        RT_CASE_RET_STR(GUEST_DND_FN_GH_SND_DATA);
        RT_CASE_RET_STR(GUEST_DND_FN_GH_SND_DIR);
        RT_CASE_RET_STR(GUEST_DND_FN_GH_SND_FILE_DATA);
        RT_CASE_RET_STR(GUEST_DND_FN_GH_SND_FILE_HDR);
        default:
            break;
    }
    return "unknown";
}

/**
 * Converts a VBOXDNDACTION to a string.
 *
 * @returns Stringified version of VBOXDNDACTION
 * @param   uAction             DnD action to convert.
 */
const char *DnDActionToStr(VBOXDNDACTION uAction)
{
    switch (uAction)
    {
        case VBOX_DND_ACTION_IGNORE: return "ignore";
        case VBOX_DND_ACTION_COPY:   return "copy";
        case VBOX_DND_ACTION_MOVE:   return "move";
        case VBOX_DND_ACTION_LINK:   return "link";
        default:
            break;
    }
    AssertMsgFailedReturn(("Unknown uAction=%d\n", uAction), "bad");
}

/**
 * Converts a VBOXDNDACTIONLIST to a string.
 *
 * @returns Stringified version of VBOXDNDACTIONLIST. Must be free'd by the caller using RTStrFree().
 * @retval  NULL on allocation failure.
 * @retval  "<None>" if no (valid) actions found.
 * @param   fActionList         DnD action list to convert.
 */
char *DnDActionListToStrA(VBOXDNDACTIONLIST fActionList)
{
    char *pszList = NULL;

#define HANDLE_ACTION(a_Action) \
    if (fActionList & a_Action) \
    { \
        if (pszList) \
            AssertRCReturn(RTStrAAppend(&pszList, ", "), NULL); \
        AssertRCReturn(RTStrAAppend(&pszList, DnDActionToStr(a_Action)), NULL); \
    }

    HANDLE_ACTION(VBOX_DND_ACTION_IGNORE);
    HANDLE_ACTION(VBOX_DND_ACTION_COPY);
    HANDLE_ACTION(VBOX_DND_ACTION_MOVE);
    HANDLE_ACTION(VBOX_DND_ACTION_LINK);

#undef HANDLE_ACTION

    if (!pszList)
        AssertRCReturn(RTStrAAppend(&pszList, "<None>"), NULL);

    return pszList;
}

/**
 * Converts a VBOXDNDSTATE to a string.
 *
 * @returns Stringified version of VBOXDNDSTATE.
 * @param   enmState            DnD state to convert.
 */
const char *DnDStateToStr(VBOXDNDSTATE enmState)
{
    switch (enmState)
    {
        case VBOXDNDSTATE_UNKNOWN:       return "unknown";
        case VBOXDNDSTATE_ENTERED:       return "entered VM window";
        case VBOXDNDSTATE_LEFT:          return "left VM window";
        case VBOXDNDSTATE_QUERY_FORMATS: return "querying formats";
        case VBOXDNDSTATE_QUERY_STATUS:  return "querying status";
        case VBOXDNDSTATE_DRAGGING:      return "dragging";
        case VBOXDNDSTATE_DROP_STARTED:  return "drop started";
        case VBOXDNDSTATE_DROP_ENDED:    return "drop ended";
        case VBOXDNDSTATE_CANCELLED:     return "cancelled";
        case VBOXDNDSTATE_ERROR:         return "error";
        default:
            break;
    }
    AssertMsgFailedReturn(("Unknown enmState=%d\n", enmState), "bad");
}

