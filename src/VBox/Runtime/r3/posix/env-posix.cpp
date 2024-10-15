/* $Id: env-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Environment, Posix.
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
#ifdef RT_OS_DARWIN
/* pick the correct prototype for unsetenv. */
# define _POSIX_C_SOURCE 1
#endif
#include <iprt/env.h>

#include <iprt/alloca.h>
#include <iprt/assert.h>
#if defined(DEBUG) && defined(RT_OS_LINUX)
# include <iprt/asm.h>
#endif
#include <iprt/err.h>
#include <iprt/string.h>

#include <stdlib.h>
#include <errno.h>

#include "internal/alignmentchecks.h"


RTDECL(bool) RTEnvExistsBad(const char *pszVar)
{
    AssertReturn(strchr(pszVar, '=') == NULL, false);
    return RTEnvGetBad(pszVar) != NULL;
}


RTDECL(bool) RTEnvExist(const char *pszVar)
{
    return RTEnvExistsBad(pszVar);
}


RTDECL(const char *) RTEnvGetBad(const char *pszVar)
{
    AssertReturn(strchr(pszVar, '=') == NULL, NULL);

    IPRT_ALIGNMENT_CHECKS_DISABLE(); /* glibc causes trouble */
    const char *pszValue = getenv(pszVar);
    IPRT_ALIGNMENT_CHECKS_ENABLE();
    return pszValue;
}


RTDECL(const char *) RTEnvGet(const char *pszVar)
{
    return RTEnvGetBad(pszVar);
}


RTDECL(int) RTEnvPutBad(const char *pszVarEqualValue)
{
    /** @todo putenv is a source memory leaks. deal with this on a per system basis. */
    if (!putenv((char *)pszVarEqualValue))
        return 0;
    return RTErrConvertFromErrno(errno);
}


RTDECL(int) RTEnvPut(const char *pszVarEqualValue)
{
    return RTEnvPutBad(pszVarEqualValue);
}


RTDECL(int) RTEnvSetBad(const char *pszVar, const char *pszValue)
{
    AssertMsgReturn(strchr(pszVar, '=') == NULL, ("'%s'\n", pszVar), VERR_ENV_INVALID_VAR_NAME);

#if defined(_MSC_VER)
    /* make a local copy and feed it to putenv. */
    const size_t cchVar = strlen(pszVar);
    const size_t cchValue = strlen(pszValue);
    char *pszTmp = (char *)alloca(cchVar + cchValue + 2 + !*pszValue);
    memcpy(pszTmp, pszVar, cchVar);
    pszTmp[cchVar] = '=';
    if (*pszValue)
        memcpy(pszTmp + cchVar + 1, pszValue, cchValue + 1);
    else
    {
        pszTmp[cchVar + 1] = ' '; /* wrong, but putenv will remove it otherwise. */
        pszTmp[cchVar + 2] = '\0';
    }

    if (!putenv(pszTmp))
        return 0;
    return RTErrConvertFromErrno(errno);

#else
    if (!setenv(pszVar, pszValue, 1))
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
#endif
}


RTDECL(int) RTEnvSet(const char *pszVar, const char *pszValue)
{
    return RTEnvSetBad(pszVar, pszValue);
}

RTDECL(int) RTEnvUnsetBad(const char *pszVar)
{
    AssertReturn(strchr(pszVar, '=') == NULL, VERR_ENV_INVALID_VAR_NAME);

    /*
     * Check that it exists first.
     */
    if (!RTEnvExist(pszVar))
        return VINF_ENV_VAR_NOT_FOUND;

    /*
     * Ok, try remove it.
     */
#ifdef RT_OS_WINDOWS
    /* Use putenv(var=) since Windows does not have unsetenv(). */
    size_t cchVar = strlen(pszVar);
    char *pszBuf = (char *)alloca(cchVar + 2);
    memcpy(pszBuf, pszVar, cchVar);
    pszBuf[cchVar]     = '=';
    pszBuf[cchVar + 1] = '\0';

    if (!putenv(pszBuf))
        return VINF_SUCCESS;

#else
    /* This is the preferred function as putenv() like used above does neither work on Solaris nor on Darwin. */
    if (!unsetenv((char*)pszVar))
        return VINF_SUCCESS;
#endif

    return RTErrConvertFromErrno(errno);
}

RTDECL(int) RTEnvUnset(const char *pszVar)
{
    return RTEnvUnsetBad(pszVar);
}

