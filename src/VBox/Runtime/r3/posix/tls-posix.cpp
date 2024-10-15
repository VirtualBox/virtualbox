/* $Id: tls-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Thread Local Storage (TLS), POSIX.
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
#include <errno.h>
#include <pthread.h>

#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>


AssertCompile(sizeof(pthread_key_t) <= sizeof(RTTLS));


RTR3DECL(RTTLS) RTTlsAlloc(void)
{
    pthread_key_t iTls = (pthread_key_t)NIL_RTTLS;
    int rc = pthread_key_create(&iTls, NULL);
    if (!rc)
    {
        Assert(iTls != (pthread_key_t)NIL_RTTLS);
        return iTls;
    }
    return NIL_RTTLS;
}


RTR3DECL(int) RTTlsAllocEx(PRTTLS piTls, PFNRTTLSDTOR pfnDestructor)
{
    pthread_key_t iTls = (pthread_key_t)NIL_RTTLS;
#if defined(__GNUC__) && defined(RT_ARCH_X86)
    int rc = pthread_key_create(&iTls, (void (*)(void*))pfnDestructor);
#else
    int rc = pthread_key_create(&iTls, pfnDestructor);
#endif
    if (!rc)
    {
        *piTls = iTls;
        Assert((pthread_key_t)*piTls == iTls);
        Assert(*piTls != NIL_RTTLS);
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(rc);
}


RTR3DECL(int) RTTlsFree(RTTLS iTls)
{
    if (iTls == NIL_RTTLS)
        return VINF_SUCCESS;
    int rc = pthread_key_delete(iTls);
    if (!rc)
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(rc);
}


RTR3DECL(void *) RTTlsGet(RTTLS iTls)
{
    return pthread_getspecific(iTls);
}


RTR3DECL(int) RTTlsGetEx(RTTLS iTls, void **ppvValue)
{
    if (RT_UNLIKELY(iTls == NIL_RTTLS))
        return VERR_INVALID_PARAMETER;
    *ppvValue = pthread_getspecific(iTls);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTTlsSet(RTTLS iTls, void *pvValue)
{
    int rc = pthread_setspecific(iTls, pvValue);
    if (RT_UNLIKELY(rc != 0))
        return RTErrConvertFromErrno(rc);
    return VINF_SUCCESS;
}

