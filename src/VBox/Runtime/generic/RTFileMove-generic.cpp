/* $Id: RTFileMove-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileMove, Generic.
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
#define LOG_GROUP RTLOGGROUP_FILE
#include <iprt/file.h>
#include "internal/iprt.h"

#include <iprt/path.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/log.h>


RTDECL(int) RTFileMove(const char *pszSrc, const char *pszDst, unsigned fMove)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(*pszDst, ("%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fMove & ~RTFILEMOVE_FLAGS_REPLACE), ("%#x\n", fMove), VERR_INVALID_PARAMETER);

    /*
     * Try RTFileRename first.
     */
    Assert(RTPATHRENAME_FLAGS_REPLACE == RTFILEMOVE_FLAGS_REPLACE);
    unsigned fRename = fMove;
    int rc = RTFileRename(pszSrc, pszDst, fRename);
    if (rc == VERR_NOT_SAME_DEVICE)
    {
        const char *pszDelete = NULL;

        /*
         * The source and target are not on the same device, darn.
         * We'll try open both ends and perform a copy.
         */
        RTFILE FileSrc;
        rc = RTFileOpen(&FileSrc, pszSrc, RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN);
        if (RT_SUCCESS(rc))
        {
            RTFILE FileDst;
            rc = RTFileOpen(&FileDst, pszDst, RTFILE_O_WRITE | RTFILE_O_DENY_ALL | RTFILE_O_CREATE_REPLACE);
            if (RT_SUCCESS(rc))
            {
                rc = RTFileCopyByHandles(FileSrc, FileDst);
                if (RT_SUCCESS(rc))
                    pszDelete = pszSrc;
                else
                {
                    pszDelete = pszDst;
                    Log(("RTFileMove('%s', '%s', %#x): copy failed, rc=%Rrc\n",
                         pszSrc, pszDst, fMove, rc));
                }

                /* try delete without closing, and could perhaps avoid some trouble */
                int rc2 = RTFileDelete(pszDelete);
                if (RT_SUCCESS(rc2))
                    pszDelete = NULL;
                RTFileClose(FileDst);
            }
            else
                Log(("RTFileMove('%s', '%s', %#x): failed to create destination, rc=%Rrc\n",
                     pszSrc, pszDst, fMove, rc));
            RTFileClose(FileSrc);
        }
        else
            Log(("RTFileMove('%s', '%s', %#x): failed to open source, rc=%Rrc\n",
                 pszSrc, pszDst, fMove, rc));

        /* if we failed to close it while open, close it now */
        if (pszDelete)
        {
            int rc2 = RTFileDelete(pszDelete);
            if (RT_FAILURE(rc2))
                Log(("RTFileMove('%s', '%s', %#x): failed to delete '%s', rc2=%Rrc (rc=%Rrc)\n",
                     pszSrc, pszDst, fMove, pszDelete, rc2, rc));
        }
    }

    LogFlow(("RTDirRename(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n",
             pszSrc, pszSrc, pszDst, pszDst, fMove, rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTFileMove);

