/* $Id: tracedefault.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Tracebuffer common functions.
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
#include "internal/iprt.h"
#include <iprt/trace.h>

#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The default trace buffer handle. */
static RTTRACEBUF   g_hDefaultTraceBuf = NIL_RTTRACEBUF;



RTDECL(int)         RTTraceSetDefaultBuf(RTTRACEBUF hTraceBuf)
{
    /* Retain the new buffer. */
    if (hTraceBuf != NIL_RTTRACEBUF)
    {
        uint32_t cRefs = RTTraceBufRetain(hTraceBuf);
        if (cRefs >= _1M)
            return VERR_INVALID_HANDLE;
    }

    RTTRACEBUF hOldTraceBuf;
#ifdef IN_RC
    hOldTraceBuf = (RTTRACEBUF)ASMAtomicXchgPtr((void **)&g_hDefaultTraceBuf, hTraceBuf);
#else
    ASMAtomicXchgHandle(&g_hDefaultTraceBuf, hTraceBuf, &hOldTraceBuf);
#endif

    if (    hOldTraceBuf != NIL_RTTRACEBUF
        &&  hOldTraceBuf != hTraceBuf)
    {
        /* Race prevention kludge. */
#ifndef IN_RC
        RTThreadSleep(33);
#endif
        RTTraceBufRelease(hOldTraceBuf);
    }

    return VINF_SUCCESS;
}


RTDECL(RTTRACEBUF)  RTTraceGetDefaultBuf(void)
{
    return g_hDefaultTraceBuf;
}

