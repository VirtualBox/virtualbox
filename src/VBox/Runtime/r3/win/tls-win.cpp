/* $Id: tls-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Thread Local Storage (TLS), Win32.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_THREAD
#include <iprt/win/windows.h>

#include <iprt/thread.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTTLSWINDTOR
{
    RTLISTNODE      ListEntry;
    DWORD           iTls;
    PFNRTTLSDTOR    pfnDestructor;
} RTTLSWINDTOR;
typedef RTTLSWINDTOR *PRTTLSWINDTOR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init once for the list and critical section. */
static RTONCE               g_Once = RTONCE_INITIALIZER;
/** Critical section protecting the TLS destructor list. */
static RTCRITSECTRW         g_CritSect;
/** List of TLS destrictors (RTTLSWINDTOR).  */
static RTLISTANCHOR         g_TlsDtorHead;
/** Number of desturctors in the list (helps putting of initialization). */
static uint32_t volatile    g_cTlsDtors = 0;


/**
 * @callback_method_impl{FNRTONCE}
 */
static DECLCALLBACK(int32_t) rtTlsWinInitLock(void *pvUser)
{
    RT_NOREF(pvUser);
    RTListInit(&g_TlsDtorHead);
    return RTCritSectRwInit(&g_CritSect);
}


RTR3DECL(RTTLS) RTTlsAlloc(void)
{
    AssertCompile(sizeof(RTTLS) >= sizeof(DWORD));
    DWORD iTls = TlsAlloc();
    return iTls != TLS_OUT_OF_INDEXES ? (RTTLS)iTls : NIL_RTTLS;
}


RTR3DECL(int) RTTlsAllocEx(PRTTLS piTls, PFNRTTLSDTOR pfnDestructor)
{
    int rc;
    if (!pfnDestructor)
    {
        DWORD iTls = TlsAlloc();
        if (iTls != TLS_OUT_OF_INDEXES)
        {
            Assert((RTTLS)iTls != NIL_RTTLS);
            *piTls = (RTTLS)iTls;
            Assert((DWORD)*piTls == iTls);
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        rc = RTOnce(&g_Once, rtTlsWinInitLock, NULL);
        if (RT_SUCCESS(rc))
        {
            PRTTLSWINDTOR pDtor = (PRTTLSWINDTOR)RTMemAlloc(sizeof(*pDtor));
            if (pDtor)
            {
                DWORD iTls = TlsAlloc();
                if (iTls != TLS_OUT_OF_INDEXES)
                {
                    Assert((RTTLS)iTls != NIL_RTTLS);
                    *piTls = (RTTLS)iTls;
                    Assert((DWORD)*piTls == iTls);

                    /*
                     * Add the destructor to the list.  We keep it sorted.
                     */
                    pDtor->iTls = iTls;
                    pDtor->pfnDestructor = pfnDestructor;
                    RTCritSectRwEnterExcl(&g_CritSect);
                    RTListAppend(&g_TlsDtorHead, &pDtor->ListEntry);
                    ASMAtomicIncU32(&g_cTlsDtors);
                    RTCritSectRwLeaveExcl(&g_CritSect);

                    rc = VINF_SUCCESS;
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }
    return rc;
}


RTR3DECL(int) RTTlsFree(RTTLS iTls)
{
    if (iTls == NIL_RTTLS)
        return VINF_SUCCESS;
    if (TlsFree((DWORD)iTls))
    {
        if (ASMAtomicReadU32(&g_cTlsDtors) > 0)
        {
            RTCritSectRwEnterExcl(&g_CritSect);
            PRTTLSWINDTOR pDtor;
            RTListForEach(&g_TlsDtorHead, pDtor, RTTLSWINDTOR, ListEntry)
            {
                if (pDtor->iTls == (DWORD)iTls)
                {
                    RTListNodeRemove(&pDtor->ListEntry);
                    ASMAtomicDecU32(&g_cTlsDtors);
                    RTMemFree(pDtor);
                    break;
                }
            }
            RTCritSectRwLeaveExcl(&g_CritSect);
        }
        return VINF_SUCCESS;
    }
    return RTErrConvertFromWin32(GetLastError());
}


RTR3DECL(void *) RTTlsGet(RTTLS iTls)
{
    return TlsGetValue((DWORD)iTls);
}


RTR3DECL(int) RTTlsGetEx(RTTLS iTls, void **ppvValue)
{
    void *pv = TlsGetValue((DWORD)iTls);
    if (pv)
    {
        *ppvValue = pv;
        return VINF_SUCCESS;
    }

    /* TlsGetValue always updates last error */
    *ppvValue = NULL;
    return RTErrConvertFromWin32(GetLastError());
}


RTR3DECL(int) RTTlsSet(RTTLS iTls, void *pvValue)
{
    if (TlsSetValue((DWORD)iTls, pvValue))
        return VINF_SUCCESS;
    return RTErrConvertFromWin32(GetLastError());
}


/**
 * Called by dllmain-win.cpp when a thread detaches.
 */
DECLHIDDEN(void) rtThreadWinTlsDestruction(void)
{
    if (ASMAtomicReadU32(&g_cTlsDtors) > 0)
    {
        RTCritSectRwEnterShared(&g_CritSect);
        PRTTLSWINDTOR pDtor;
        RTListForEach(&g_TlsDtorHead, pDtor, RTTLSWINDTOR, ListEntry)
        {
            void *pvValue = TlsGetValue(pDtor->iTls);
            if (pvValue != NULL)
            {
                pDtor->pfnDestructor(pvValue);
                TlsSetValue(pDtor->iTls, NULL);
            }
        }
        RTCritSectRwLeaveShared(&g_CritSect);
    }
}

