/* $Id: errvars-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Save and Restore Error Variables, POSIX Ring-3.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <netdb.h>
#include <errno.h>

#include <iprt/errcore.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include "internal/magics.h"



RTDECL(PRTERRVARS) RTErrVarsSave(PRTERRVARS pVars)
{
    pVars->ai32Vars[0] = RTERRVARS_MAGIC;
    pVars->ai32Vars[1] = errno;
    pVars->ai32Vars[2] = h_errno;
    return pVars;
}


RTDECL(void) RTErrVarsRestore(PCRTERRVARS pVars)
{
    AssertReturnVoid(pVars->ai32Vars[0] == RTERRVARS_MAGIC);
    h_errno = pVars->ai32Vars[2];
    errno = pVars->ai32Vars[1];
}


RTDECL(bool) RTErrVarsAreEqual(PCRTERRVARS pVars1, PCRTERRVARS pVars2)
{
    Assert(pVars1->ai32Vars[0] == RTERRVARS_MAGIC);
    Assert(pVars2->ai32Vars[0] == RTERRVARS_MAGIC);

    return pVars1->ai32Vars[0] == pVars2->ai32Vars[0]
        && pVars1->ai32Vars[1] == pVars2->ai32Vars[1]
        && pVars1->ai32Vars[2] == pVars2->ai32Vars[2];
}


RTDECL(bool) RTErrVarsHaveChanged(PCRTERRVARS pVars)
{
    Assert(pVars->ai32Vars[0] == RTERRVARS_MAGIC);

    return pVars->ai32Vars[0] != RTERRVARS_MAGIC
        || pVars->ai32Vars[1] != errno
        || pVars->ai32Vars[2] != h_errno;
}

