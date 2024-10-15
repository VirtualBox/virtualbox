/* $Id: VBoxCredProvUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxCredProvUtils - Misc. utility functions for VBoxCredProv.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>

#include <iprt/string.h>
#include <VBox/log.h>
#ifdef LOG_ENABLED
# include <iprt/stream.h>
#endif
#include <VBox/VBoxGuestLib.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Verbosity flag for guest logging. */
DWORD g_dwVerbosity = 0;


/**
 * Displays a verbose message.
 *
 * @param   dwLevel     Minimum log level required to display this message.
 * @param   pszFormat   The message text.
 * @param   ...         Format arguments.
 */
void VBoxCredProvVerbose(DWORD dwLevel, const char *pszFormat, ...)
{
    if (dwLevel <= g_dwVerbosity)
    {
        va_list args;
        va_start(args, pszFormat);
        char *psz = NULL;
        RTStrAPrintfV(&psz, pszFormat, args);
        va_end(args);

        AssertPtr(psz);
        LogRel(("%s", psz));

#ifdef LOG_ENABLED
        PRTSTREAM pStream;
        int rc2 = RTStrmOpen("C:\\VBoxCredProvLog.txt", "a", &pStream);
        if (RT_SUCCESS(rc2))
        {
            RTStrmPrintf(pStream, "%s", psz);
            RTStrmClose(pStream);
        }
#endif
        RTStrFree(psz);
    }
}


/**
 * Reports VBoxGINA's status to the host (treated as a guest facility).
 *
 * @return  IPRT status code.
 * @param   enmStatus               Status to report to the host.
 */
int VBoxCredProvReportStatus(VBoxGuestFacilityStatus enmStatus)
{
    VBoxCredProvVerbose(0, "VBoxCredProv: reporting status %d\n", enmStatus);

    int rc = VbglR3AutoLogonReportStatus(enmStatus);
    if (RT_FAILURE(rc))
        VBoxCredProvVerbose(0, "VBoxCredProv: failed to report status %d, rc=%Rrc\n", enmStatus, rc);
    return rc;
}

