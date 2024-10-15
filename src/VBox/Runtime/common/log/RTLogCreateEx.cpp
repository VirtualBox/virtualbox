/* $Id: RTLogCreateEx.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Runtime VBox - RTLogCreateEx.
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
#include <iprt/log.h>
#include "internal/iprt.h"

#include <iprt/stdarg.h>



RTDECL(int) RTLogCreateEx(PRTLOGGER *ppLogger, const char *pszEnvVarBase, uint64_t fFlags, const char *pszGroupSettings,
                          unsigned cGroups, const char * const *papszGroups, uint32_t cMaxEntriesPerGroup,
                          uint32_t cBufDescs, PRTLOGBUFFERDESC paBufDescs, uint32_t fDestFlags,
                          PFNRTLOGPHASE pfnPhase, uint32_t cHistory, uint64_t cbHistoryFileMax, uint32_t cSecsHistoryTimeSlot,
                          PCRTLOGOUTPUTIF pOutputIf, void *pvOutputIfUser,
                          PRTERRINFO pErrInfo, const char *pszFilenameFmt, ...)
{
    va_list va;
    int     rc;

    va_start(va, pszFilenameFmt);
    rc = RTLogCreateExV(ppLogger, pszEnvVarBase, fFlags, pszGroupSettings, cGroups, papszGroups, cMaxEntriesPerGroup,
                        cBufDescs, paBufDescs, fDestFlags,
                        pfnPhase, cHistory, cbHistoryFileMax, cSecsHistoryTimeSlot,
                        pOutputIf, pvOutputIfUser,
                        pErrInfo, pszFilenameFmt, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTLogCreateEx);

