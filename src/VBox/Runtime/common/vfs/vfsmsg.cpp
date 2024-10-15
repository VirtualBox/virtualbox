/* $Id: vfsmsg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, Error Messaging for Chains.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/vfs.h>

#include <iprt/errcore.h>
#include <iprt/message.h>


RTDECL(void) RTVfsChainMsgError(const char *pszFunction, const char *pszSpec, int rc, uint32_t offError, PRTERRINFO pErrInfo)
{
    if (RTErrInfoIsSet(pErrInfo))
    {
        if (offError > 0)
            RTMsgError("%s failed with rc=%Rrc: %s\n"
                       "    '%s'\n"
                       "     %*s^\n",
                       pszFunction, rc, pErrInfo->pszMsg, pszSpec, offError, "");
        else
            RTMsgError("%s failed to open '%s': %Rrc: %s\n", pszFunction, pszSpec, rc, pErrInfo->pszMsg);
    }
    else
    {
        if (offError > 0)
            RTMsgError("%s failed with rc=%Rrc:\n"
                       "    '%s'\n"
                       "     %*s^\n",
                       pszFunction, rc, pszSpec, offError, "");
        else
            RTMsgError("%s failed to open '%s': %Rrc\n", pszFunction, pszSpec, rc);
    }
}


RTDECL(RTEXITCODE) RTVfsChainMsgErrorExitFailure(const char *pszFunction, const char *pszSpec,
                                                 int rc, uint32_t offError, PRTERRINFO pErrInfo)
{
    RTVfsChainMsgError(pszFunction, pszSpec, rc, offError, pErrInfo);
    return RTEXITCODE_FAILURE;
}

