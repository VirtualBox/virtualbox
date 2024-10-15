/* $Id: env-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Environment, Generic.
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
#include <iprt/env.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/err.h>
#include <iprt/sort.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include "internal/magics.h"

#ifdef RT_OS_WINDOWS
# include <iprt/nt/nt.h>
#else
# include <stdlib.h>
# if !defined(RT_OS_WINDOWS)
#  include <unistd.h>
# endif
# ifdef RT_OS_DARWIN
#  include <crt_externs.h>
# endif
# if defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD) || defined(RT_OS_OPENBSD)
RT_C_DECLS_BEGIN
extern char **environ;
RT_C_DECLS_END
# endif
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The allocation granularity of the RTENVINTERNAL::papszEnv memory. */
#define RTENV_GROW_SIZE     16

/** Macro that unlocks the specified environment block. */
#define RTENV_LOCK(pEnvInt)     do { } while (0)
/** Macro that unlocks the specified environment block. */
#define RTENV_UNLOCK(pEnvInt)   do { } while (0)

/** @def RTENV_IMPLEMENTS_UTF8_DEFAULT_ENV_API
 * Indicates the RTEnv*Utf8 APIs are implemented. */
#if defined(RT_OS_WINDOWS) || defined(DOXYGEN_RUNNING)
# define RTENV_IMPLEMENTS_UTF8_DEFAULT_ENV_API 1
#endif

/** @def RTENV_ALLOW_EQUAL_FIRST_IN_VAR
 * Allows a variable to start with an '=' sign by default.  This is used by
 * windows to maintain CWDs of non-current drives.
 * @note Not supported by _wputenv AFAIK. */
#if defined(RT_OS_WINDOWS) || defined(DOXYGEN_RUNNING)
# define RTENV_ALLOW_EQUAL_FIRST_IN_VAR 1
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The internal representation of a (non-default) environment.
 */
typedef struct RTENVINTERNAL
{
    /** Magic value . */
    uint32_t    u32Magic;
    /** Set if this is a record of environment changes, putenv style. */
    bool        fPutEnvBlock;
    /** Set if starting a variable with an equal sign is okay, clear if not okay
     * (RTENV_CREATE_F_ALLOW_EQUAL_FIRST_IN_VAR). */
    bool        fFirstEqual;
    /** Number of variables in the array.
     * This does not include the terminating NULL entry. */
    size_t      cVars;
    /** Capacity (allocated size) of the array.
     * This includes space for the terminating NULL element (for compatibility
     * with the C library), so that c <= cCapacity - 1. */
    size_t      cAllocated;
    /** Array of environment variables.
     * These are always in "NAME=VALUE" form, where the value can be empty. If
     * fPutEnvBlock is set though, there will be "NAME" entries too for variables
     * that need to be removed when merged with another environment block. */
    char      **papszEnv;
    /** Array of environment variables in the process CP.
     * This get (re-)constructed when RTEnvGetExecEnvP method is called. */
    char      **papszEnvOtherCP;

    /** The compare function we're using. */
    DECLCALLBACKMEMBER(int, pfnCompare,(const char *psz1, const char *psz2, size_t cchMax));
} RTENVINTERNAL, *PRTENVINTERNAL;


#ifndef RT_OS_WINDOWS
/**
 * Internal worker that resolves the pointer to the default
 * process environment. (environ)
 *
 * @returns Pointer to the default environment.
 *          This may be NULL.
 */
static const char * const *rtEnvDefault(void)
{
# ifdef RT_OS_DARWIN
    return *(_NSGetEnviron());
# else
    return environ;
# endif
}
#endif


/**
 * Internal worker that creates an environment handle with a specified capacity.
 *
 * @returns IPRT status code.
 * @param   ppIntEnv        Where to store the result.
 * @param   cAllocated      The initial array size.
 * @param   fCaseSensitive  Whether the environment block is case sensitive or
 *                          not.
 * @param   fPutEnvBlock    Indicates whether this is a special environment
 *                          block that will be used to record change another
 *                          block.  We will keep unsets in putenv format, i.e.
 *                          just the variable name without any equal sign.
 * @param   fFirstEqual     The RTENV_CREATE_F_ALLOW_EQUAL_FIRST_IN_VAR value.
                                                      */
static int rtEnvCreate(PRTENVINTERNAL *ppIntEnv, size_t cAllocated, bool fCaseSensitive, bool fPutEnvBlock, bool fFirstEqual)
{
    /*
     * Allocate environment handle.
     */
    PRTENVINTERNAL pIntEnv = (PRTENVINTERNAL)RTMemAlloc(sizeof(*pIntEnv));
    if (pIntEnv)
    {
        /*
         * Pre-allocate the variable array.
         */
        pIntEnv->u32Magic = RTENV_MAGIC;
        pIntEnv->fPutEnvBlock = fPutEnvBlock;
        pIntEnv->fFirstEqual = fFirstEqual;
        pIntEnv->pfnCompare = fCaseSensitive ? RTStrNCmp : RTStrNICmp;
        pIntEnv->papszEnvOtherCP = NULL;
        pIntEnv->cVars = 0;
        pIntEnv->cAllocated = RT_ALIGN_Z(RT_MAX(cAllocated, RTENV_GROW_SIZE), RTENV_GROW_SIZE);
        pIntEnv->papszEnv = (char **)RTMemAllocZ(sizeof(pIntEnv->papszEnv[0]) * pIntEnv->cAllocated);
        if (pIntEnv->papszEnv)
        {
            *ppIntEnv = pIntEnv;
            return VINF_SUCCESS;
        }

        RTMemFree(pIntEnv);
    }

    return VERR_NO_MEMORY;
}


RTDECL(int) RTEnvCreate(PRTENV pEnv)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);
#ifdef RTENV_ALLOW_EQUAL_FIRST_IN_VAR
    return rtEnvCreate(pEnv, RTENV_GROW_SIZE, true /*fCaseSensitive*/, false /*fPutEnvBlock*/, true /*fFirstEqual*/);
#else
    return rtEnvCreate(pEnv, RTENV_GROW_SIZE, true /*fCaseSensitive*/, false /*fPutEnvBlock*/, false/*fFirstEqual*/);
#endif
}
RT_EXPORT_SYMBOL(RTEnvCreate);


RTDECL(int) RTEnvCreateEx(PRTENV phEnv, uint32_t fFlags)
{
    AssertPtrReturn(phEnv, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTENV_CREATE_F_VALID_MASK), VERR_INVALID_FLAGS);
    return rtEnvCreate(phEnv, RTENV_GROW_SIZE, true /*fCaseSensitive*/, false /*fPutEnvBlock*/,
                       RT_BOOL(fFlags & RTENV_CREATE_F_ALLOW_EQUAL_FIRST_IN_VAR));
}
RT_EXPORT_SYMBOL(RTEnvCreateEx);


RTDECL(int) RTEnvDestroy(RTENV Env)
{
    /*
     * Ignore NIL_RTENV and validate input.
     */
    if (    Env == NIL_RTENV
        ||  Env == RTENV_DEFAULT)
        return VINF_SUCCESS;

    PRTENVINTERNAL pIntEnv = Env;
    AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
    AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Do the cleanup.
     */
    RTENV_LOCK(pIntEnv);
    pIntEnv->u32Magic++;
    size_t iVar = pIntEnv->cVars;
    while (iVar-- > 0)
        RTStrFree(pIntEnv->papszEnv[iVar]);
    RTMemFree(pIntEnv->papszEnv);
    pIntEnv->papszEnv = NULL;

    if (pIntEnv->papszEnvOtherCP)
    {
        for (iVar = 0; pIntEnv->papszEnvOtherCP[iVar]; iVar++)
        {
            RTStrFree(pIntEnv->papszEnvOtherCP[iVar]);
            pIntEnv->papszEnvOtherCP[iVar] = NULL;
        }
        RTMemFree(pIntEnv->papszEnvOtherCP);
        pIntEnv->papszEnvOtherCP = NULL;
    }

    RTENV_UNLOCK(pIntEnv);
    /*RTCritSectDelete(&pIntEnv->CritSect) */
    RTMemFree(pIntEnv);

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTEnvDestroy);


static int rtEnvCloneDefault(PRTENV phEnv)
{
#ifdef RTENV_ALLOW_EQUAL_FIRST_IN_VAR
    bool const fFirstEqual = true;
#else
    bool const fFirstEqual = false;
#endif

#ifdef RT_OS_WINDOWS
    /*
     * Lock the PEB, get the process environment.
     *
     * On older windows version GetEnviornmentStringsW will not copy the
     * environment block, but return the pointer stored in the PEB.  This
     * should be safer wrt to concurrent changes.
     */
    PPEB pPeb = RTNtCurrentPeb();

    RtlAcquirePebLock();

    /* Count variables in the block: */
    size_t    cVars    = 0;
    PCRTUTF16 pwszzEnv = pPeb->ProcessParameters ? pPeb->ProcessParameters->Environment : NULL;
    if (pwszzEnv)
    {
        PCRTUTF16 pwsz = pwszzEnv;
        while (*pwsz)
        {
            cVars++;
            pwsz += RTUtf16Len(pwsz) + 1;
        }
    }

    PRTENVINTERNAL pIntEnv;
    int rc = rtEnvCreate(&pIntEnv, cVars + 1 /* NULL */, false /*fCaseSensitive*/, false /*fPutEnvBlock*/, fFirstEqual);
    if (RT_SUCCESS(rc))
    {
        size_t iDst;
        for (iDst = 0; iDst < cVars && *pwszzEnv; iDst++, pwszzEnv += RTUtf16Len(pwszzEnv) + 1)
        {
            int rc2 = RTUtf16ToUtf8(pwszzEnv, &pIntEnv->papszEnv[iDst]);
            if (RT_SUCCESS(rc2))
            {
                /* Make sure it contains an '='. */
                if (strchr(pIntEnv->papszEnv[iDst], '='))
                    continue;
                rc2 = RTStrAAppend(&pIntEnv->papszEnv[iDst], "=");
                if (RT_SUCCESS(rc2))
                    continue;
            }

            /* failed fatally. */
            pIntEnv->cVars = iDst + 1;
            RtlReleasePebLock();
            RTEnvDestroy(pIntEnv);
            return rc2;
        }

        Assert(!*pwszzEnv); Assert(iDst == cVars);
        pIntEnv->cVars = iDst;
        pIntEnv->papszEnv[iDst] = NULL;

        /* done */
        *phEnv = pIntEnv;
    }

    RtlReleasePebLock();
    return rc;

#else /* !RT_OS_WINDOWS */

    /*
     * Figure out how many variable to clone.
     */
    const char * const *papszEnv = rtEnvDefault();
    size_t              cVars = 0;
    if (papszEnv)
        while (papszEnv[cVars])
            cVars++;

    bool fCaseSensitive = true;
# if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
    /* DOS systems was case insensitive.  A prime example is the 'Path'
       variable on windows which turns into the 'PATH' variable. */
    fCaseSensitive = false;
# endif

    /*
     * Create the duplicate.
     */
    PRTENVINTERNAL pIntEnv;
    int rc = rtEnvCreate(&pIntEnv, cVars + 1 /* NULL */, fCaseSensitive, false /*fPutEnvBlock*/, fFirstEqual);
    if (RT_SUCCESS(rc))
    {
        pIntEnv->cVars = cVars;
        pIntEnv->papszEnv[pIntEnv->cVars] = NULL;

        /* ASSUMES the default environment is in the current codepage. */
        size_t  iDst = 0;
        for (size_t iSrc = 0; iSrc < cVars; iSrc++)
        {
            int rc2 = RTStrCurrentCPToUtf8(&pIntEnv->papszEnv[iDst], papszEnv[iSrc]);
            if (RT_SUCCESS(rc2))
            {
                /* Make sure it contains an '='. */
                iDst++;
                if (strchr(pIntEnv->papszEnv[iDst - 1], '='))
                    continue;
                rc2 = RTStrAAppend(&pIntEnv->papszEnv[iDst - 1], "=");
                if (RT_SUCCESS(rc2))
                    continue;
            }
            else if (rc2 == VERR_NO_TRANSLATION)
            {
                rc = VWRN_ENV_NOT_FULLY_TRANSLATED;
                continue;
            }

            /* failed fatally. */
            pIntEnv->cVars = iDst;
            RTEnvDestroy(pIntEnv);
            return rc2;
        }
        pIntEnv->cVars = iDst;

        /* done */
        *phEnv = pIntEnv;
    }

    return rc;
#endif /* !RT_OS_WINDOWS */
}


/**
 * Clones a non-default environment instance.
 *
 * @param   phEnv           Where to return the handle to the cloned environment.
 * @param   pIntEnvToClone  The source environment. Caller takes care of
 *                          locking.
 */
static int rtEnvCloneNonDefault(PRTENV phEnv, PRTENVINTERNAL pIntEnvToClone)
{
    PRTENVINTERNAL pIntEnv;
    size_t const cVars = pIntEnvToClone->cVars;
    int rc = rtEnvCreate(&pIntEnv, cVars + 1 /* NULL */,
                         pIntEnvToClone->pfnCompare != RTStrNICmp,
                         pIntEnvToClone->fPutEnvBlock,
                         pIntEnvToClone->fFirstEqual);
    if (RT_SUCCESS(rc))
    {
        pIntEnv->cVars = cVars;
        pIntEnv->papszEnv[cVars] = NULL;

        const char * const * const papszEnv = pIntEnvToClone->papszEnv;
        for (size_t iVar = 0; iVar < cVars; iVar++)
        {
            char *pszVar = RTStrDup(papszEnv[iVar]);
            if (RT_UNLIKELY(!pszVar))
            {
                pIntEnv->cVars = iVar;
                RTEnvDestroy(pIntEnv);
                return VERR_NO_STR_MEMORY;
            }
            pIntEnv->papszEnv[iVar] = pszVar;
        }

        /* done */
        *phEnv = pIntEnv;
    }
    return rc;
}


RTDECL(int) RTEnvClone(PRTENV phEnv, RTENV hEnvToClone)
{
    /*
     * Validate input and what kind of source block we're working with.
     */
    int rc;
    AssertPtrReturn(phEnv, VERR_INVALID_POINTER);
    if (hEnvToClone == RTENV_DEFAULT)
        rc = rtEnvCloneDefault(phEnv);
    else
    {
        PRTENVINTERNAL pIntEnvToClone = hEnvToClone;
        AssertPtrReturn(pIntEnvToClone, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnvToClone->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);

        RTENV_LOCK(pIntEnvToClone);
        rc = rtEnvCloneNonDefault(phEnv, pIntEnvToClone);
        RTENV_UNLOCK(pIntEnvToClone);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTEnvClone);


RTDECL(int) RTEnvCloneUtf16Block(PRTENV phEnv, PCRTUTF16 pwszzBlock, uint32_t fFlags)
{
    AssertPtrReturn(pwszzBlock, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);

    /*
     * Count the number of variables in the block.
     */
    uint32_t  cVars = 0;
    PCRTUTF16 pwsz  = pwszzBlock;
    while (*pwsz != '\0')
    {
        cVars++;
        pwsz += RTUtf16Len(pwsz) + 1;
        AssertReturn(cVars < _256K, VERR_OUT_OF_RANGE);
    }

    /*
     * Create the duplicate.
     */
    PRTENVINTERNAL pIntEnv;
#ifdef RTENV_ALLOW_EQUAL_FIRST_IN_VAR
    int rc = rtEnvCreate(&pIntEnv, cVars + 1 /* NULL */, false /*fCaseSensitive*/, false /*fPutEnvBlock*/, true /*fFirstEqual*/);
#else
    int rc = rtEnvCreate(&pIntEnv, cVars + 1 /* NULL */, false /*fCaseSensitive*/, false /*fPutEnvBlock*/, false /*fFirstEqual*/);
#endif
    if (RT_SUCCESS(rc))
    {
        pIntEnv->cVars = cVars;
        pIntEnv->papszEnv[pIntEnv->cVars] = NULL;

        size_t iDst = 0;
        for (pwsz = pwszzBlock; *pwsz != '\0'; pwsz += RTUtf16Len(pwsz) + 1)
        {
            int rc2 = RTUtf16ToUtf8(pwsz, &pIntEnv->papszEnv[iDst]);
            if (RT_SUCCESS(rc2))
            {
                /* Make sure it contains an '='. */
                const char *pszEqual = strchr(pIntEnv->papszEnv[iDst], '=');
                if (!pszEqual)
                {
                    rc2 = RTStrAAppend(&pIntEnv->papszEnv[iDst], "=");
                    if (RT_SUCCESS(rc2))
                        pszEqual = strchr(pIntEnv->papszEnv[iDst], '=');

                }
                if (pszEqual)
                {
                    /* Check for duplicates, keep the last version. */
                    const char *pchVar        = pIntEnv->papszEnv[iDst];
                    size_t      cchVarNmAndEq = pszEqual - pchVar;
                    for (size_t iDst2 = 0; iDst2 < iDst; iDst2++)
                        if (pIntEnv->pfnCompare(pIntEnv->papszEnv[iDst2], pchVar, cchVarNmAndEq) == 0)
                        {
                            RTStrFree(pIntEnv->papszEnv[iDst2]);
                            pIntEnv->papszEnv[iDst2] = pIntEnv->papszEnv[iDst];
                            pIntEnv->papszEnv[iDst]  = NULL;
                            iDst--;
                            break;
                        }
                    iDst++;
                    continue;
                }
                iDst++;
            }

            /* failed fatally. */
            pIntEnv->cVars = iDst;
            RTEnvDestroy(pIntEnv);
            return rc2;
        }
        Assert(iDst <= pIntEnv->cVars);
        pIntEnv->cVars = iDst;

        /* done */
        *phEnv = pIntEnv;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTEnvCloneUtf16Block);



RTDECL(int) RTEnvReset(RTENV hEnv)
{
    PRTENVINTERNAL pIntEnv = hEnv;
    AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
    AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);

    RTENV_LOCK(pIntEnv);

    size_t iVar = pIntEnv->cVars;
    pIntEnv->cVars = 0;
    while (iVar-- > 0)
    {
        RTMemFree(pIntEnv->papszEnv[iVar]);
        pIntEnv->papszEnv[iVar] = NULL;
    }

    RTENV_UNLOCK(pIntEnv);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTEnvReset);


/**
 * Appends an already allocated string to papszEnv.
 *
 * @returns IPRT status code
 * @param   pIntEnv             The environment block to append it to.
 * @param   pszEntry            The string to add.  Already duplicated, caller
 *                              does error cleanup.
 */
static int rtEnvIntAppend(PRTENVINTERNAL pIntEnv, char *pszEntry)
{
    /*
     * Do we need to resize the array?
     */
    int rc = VINF_SUCCESS;
    size_t iVar = pIntEnv->cVars;
    if (iVar + 2 > pIntEnv->cAllocated)
    {
        void *pvNew = RTMemRealloc(pIntEnv->papszEnv, sizeof(char *) * (pIntEnv->cAllocated + RTENV_GROW_SIZE));
        if (!pvNew)
            rc = VERR_NO_MEMORY;
        else
        {
            pIntEnv->papszEnv = (char **)pvNew;
            pIntEnv->cAllocated += RTENV_GROW_SIZE;
            for (size_t iNewVar = pIntEnv->cVars; iNewVar < pIntEnv->cAllocated; iNewVar++)
                pIntEnv->papszEnv[iNewVar] = NULL;
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Append it.
         */
        pIntEnv->papszEnv[iVar] = pszEntry;
        pIntEnv->papszEnv[iVar + 1] = NULL; /* this isn't really necessary, but doesn't hurt. */
        pIntEnv->cVars = iVar + 1;
    }
    return rc;
}


/**
 * Worker for RTEnvSetEx and RTEnvPutEx.
 */
static int rtEnvSetExWorker(RTENV Env, const char *pchVar, size_t cchVar, const char *pszValue)
{
    int rc;
    if (Env == RTENV_DEFAULT)
    {
#ifdef RT_OS_WINDOWS
        extern int rtEnvSetUtf8Worker(const char *pchVar, size_t cchVar, const char *pszValue);
        rc = rtEnvSetUtf8Worker(pchVar, cchVar, pszValue);
#else
        /*
         * Since RTEnvPut isn't UTF-8 clean and actually expects the strings
         * to be in the current code page (codeset), we'll do the necessary
         * conversions here.
         */
        char *pszVarOtherCP;
        rc = RTStrUtf8ToCurrentCPEx(&pszVarOtherCP, pchVar, cchVar);
        if (RT_SUCCESS(rc))
        {
            char *pszValueOtherCP;
            rc = RTStrUtf8ToCurrentCP(&pszValueOtherCP, pszValue);
            if (RT_SUCCESS(rc))
            {
                rc = RTEnvSet(pszVarOtherCP, pszValueOtherCP);
                RTStrFree(pszValueOtherCP);
            }
            RTStrFree(pszVarOtherCP);
        }
#endif
    }
    else
    {
        PRTENVINTERNAL pIntEnv = Env;
        AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);

        /*
         * Create the variable string.
         */
        const size_t cchValue = strlen(pszValue);
        char *pszEntry = (char *)RTMemAlloc(cchVar + cchValue + 2);
        if (pszEntry)
        {
            memcpy(pszEntry, pchVar, cchVar);
            pszEntry[cchVar] = '=';
            memcpy(&pszEntry[cchVar + 1], pszValue, cchValue + 1);

            RTENV_LOCK(pIntEnv);

            /*
             * Find the location of the variable. (iVar = cVars if new)
             */
            rc = VINF_SUCCESS;
            size_t iVar;
            for (iVar = 0; iVar < pIntEnv->cVars; iVar++)
                if (    !pIntEnv->pfnCompare(pIntEnv->papszEnv[iVar], pchVar, cchVar)
                    &&  (   pIntEnv->papszEnv[iVar][cchVar] == '='
                         || pIntEnv->papszEnv[iVar][cchVar] == '\0') )
                    break;
            if (iVar < pIntEnv->cVars)
            {
                /*
                 * Replace the current entry. Simple.
                 */
                RTMemFree(pIntEnv->papszEnv[iVar]);
                pIntEnv->papszEnv[iVar] = pszEntry;
            }
            else
            {
                /*
                 * New variable, append it.
                 */
                Assert(pIntEnv->cVars == iVar);
                rc = rtEnvIntAppend(pIntEnv, pszEntry);
            }

            RTENV_UNLOCK(pIntEnv);

            if (RT_FAILURE(rc))
                RTMemFree(pszEntry);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    return rc;
}


RTDECL(int) RTEnvSetEx(RTENV Env, const char *pszVar, const char *pszValue)
{
    AssertPtrReturn(pszVar, VERR_INVALID_POINTER);
    AssertPtrReturn(pszValue, VERR_INVALID_POINTER);
    size_t const cchVar = strlen(pszVar);
    AssertReturn(cchVar > 0, VERR_ENV_INVALID_VAR_NAME);
    char const *pszEq = (char const *)memchr(pszVar, '=', cchVar);
    if (!pszEq)
    { /* likely */ }
    else
    {
        AssertReturn(Env != RTENV_DEFAULT, VERR_ENV_INVALID_VAR_NAME);
        PRTENVINTERNAL pIntEnv = Env;
        AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnv->fFirstEqual, VERR_ENV_INVALID_VAR_NAME);
        AssertReturn(memchr(pszVar + 1, '=', cchVar - 1) == NULL, VERR_ENV_INVALID_VAR_NAME);
    }

    return rtEnvSetExWorker(Env, pszVar, cchVar, pszValue);
}
RT_EXPORT_SYMBOL(RTEnvSetEx);


RTDECL(int) RTEnvUnsetEx(RTENV Env, const char *pszVar)
{
    AssertPtrReturn(pszVar, VERR_INVALID_POINTER);
    AssertReturn(*pszVar, VERR_ENV_INVALID_VAR_NAME);

    int rc;
    if (Env == RTENV_DEFAULT)
    {
#ifdef RTENV_IMPLEMENTS_UTF8_DEFAULT_ENV_API
        rc = RTEnvUnsetUtf8(pszVar);
#else
        /*
         * Since RTEnvUnset isn't UTF-8 clean and actually expects the strings
         * to be in the current code page (codeset), we'll do the necessary
         * conversions here.
         */
        char *pszVarOtherCP;
        rc = RTStrUtf8ToCurrentCP(&pszVarOtherCP, pszVar);
        if (RT_SUCCESS(rc))
        {
            rc = RTEnvUnset(pszVarOtherCP);
            RTStrFree(pszVarOtherCP);
        }
#endif
    }
    else
    {
        PRTENVINTERNAL pIntEnv = Env;
        AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);
        const size_t cchVar = strlen(pszVar);
        AssertReturn(cchVar > 0, VERR_ENV_INVALID_VAR_NAME);
        AssertReturn(strchr(pIntEnv->fFirstEqual ? pszVar + 1 : pszVar, '=') == NULL, VERR_ENV_INVALID_VAR_NAME);

        RTENV_LOCK(pIntEnv);

        /*
         * Remove all variable by the given name.
         */
        rc = VINF_ENV_VAR_NOT_FOUND;
        size_t iVar;
        for (iVar = 0; iVar < pIntEnv->cVars; iVar++)
            if (    !pIntEnv->pfnCompare(pIntEnv->papszEnv[iVar], pszVar, cchVar)
                &&  (   pIntEnv->papszEnv[iVar][cchVar] == '='
                     || pIntEnv->papszEnv[iVar][cchVar] == '\0') )
            {
                if (!pIntEnv->fPutEnvBlock)
                {
                    RTMemFree(pIntEnv->papszEnv[iVar]);
                    pIntEnv->cVars--;
                    if (pIntEnv->cVars > 0)
                        pIntEnv->papszEnv[iVar] = pIntEnv->papszEnv[pIntEnv->cVars];
                    pIntEnv->papszEnv[pIntEnv->cVars] = NULL;
                }
                else
                {
                    /* Record this unset by keeping the variable without any equal sign. */
                    pIntEnv->papszEnv[iVar][cchVar] = '\0';
                }
                rc = VINF_SUCCESS;
                /* no break, there could be more. */
            }

        /*
         * If this is a change record, we may need to add it.
         */
        if (rc == VINF_ENV_VAR_NOT_FOUND && pIntEnv->fPutEnvBlock)
        {
            char *pszEntry = (char *)RTMemDup(pszVar, cchVar + 1);
            if (pszEntry)
            {
                rc = rtEnvIntAppend(pIntEnv, pszEntry);
                if (RT_SUCCESS(rc))
                    rc = VINF_ENV_VAR_NOT_FOUND;
                else
                    RTMemFree(pszEntry);
            }
            else
                rc = VERR_NO_MEMORY;
        }

        RTENV_UNLOCK(pIntEnv);
    }
    return rc;

}
RT_EXPORT_SYMBOL(RTEnvUnsetEx);


RTDECL(int) RTEnvPutEx(RTENV Env, const char *pszVarEqualValue)
{
    int rc;
    AssertPtrReturn(pszVarEqualValue, VERR_INVALID_POINTER);
    const char *pszEq = strchr(pszVarEqualValue, '=');
    if (   pszEq == pszVarEqualValue
        && Env != RTENV_DEFAULT)
    {
        PRTENVINTERNAL pIntEnv = Env;
        AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);
        if (pIntEnv->fFirstEqual)
            pszEq = strchr(pszVarEqualValue + 1, '=');
    }
    if (!pszEq)
        rc = RTEnvUnsetEx(Env, pszVarEqualValue);
    else
    {
        AssertReturn(pszEq != pszVarEqualValue, VERR_ENV_INVALID_VAR_NAME);
        rc = rtEnvSetExWorker(Env, pszVarEqualValue, pszEq - pszVarEqualValue, pszEq + 1);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTEnvPutEx);


RTDECL(int) RTEnvGetEx(RTENV Env, const char *pszVar, char *pszValue, size_t cbValue, size_t *pcchActual)
{
    AssertPtrReturn(pszVar, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pszValue, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pcchActual, VERR_INVALID_POINTER);
    AssertReturn(pcchActual || (pszValue && cbValue), VERR_INVALID_PARAMETER);

    if (pcchActual)
        *pcchActual = 0;
    int rc;
    if (Env == RTENV_DEFAULT)
    {
#ifdef RTENV_IMPLEMENTS_UTF8_DEFAULT_ENV_API
        rc = RTEnvGetUtf8(pszVar, pszValue, cbValue, pcchActual);
#else
        /*
         * Since RTEnvGet isn't UTF-8 clean and actually expects the strings
         * to be in the current code page (codeset), we'll do the necessary
         * conversions here.
         */
        char *pszVarOtherCP;
        rc = RTStrUtf8ToCurrentCP(&pszVarOtherCP, pszVar);
        if (RT_SUCCESS(rc))
        {
            const char *pszValueOtherCP = RTEnvGet(pszVarOtherCP);
            RTStrFree(pszVarOtherCP);
            if (pszValueOtherCP)
            {
                char *pszValueUtf8;
                rc = RTStrCurrentCPToUtf8(&pszValueUtf8, pszValueOtherCP);
                if (RT_SUCCESS(rc))
                {
                    rc = VINF_SUCCESS;
                    size_t cch = strlen(pszValueUtf8);
                    if (pcchActual)
                        *pcchActual = cch;
                    if (pszValue && cbValue)
                    {
                        if (cch < cbValue)
                            memcpy(pszValue, pszValueUtf8, cch + 1);
                        else
                            rc = VERR_BUFFER_OVERFLOW;
                    }
                    RTStrFree(pszValueUtf8);
                }
            }
            else
                rc = VERR_ENV_VAR_NOT_FOUND;
        }
#endif
    }
    else
    {
        PRTENVINTERNAL pIntEnv = Env;
        AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);
        const size_t cchVar = strlen(pszVar);
        AssertReturn(cchVar > 0, VERR_ENV_INVALID_VAR_NAME);
        AssertReturn(strchr(pIntEnv->fFirstEqual ? pszVar + 1 : pszVar, '=') == NULL, VERR_ENV_INVALID_VAR_NAME);

        RTENV_LOCK(pIntEnv);

        /*
         * Locate the first variable and return it to the caller.
         */
        rc = VERR_ENV_VAR_NOT_FOUND;
        size_t iVar;
        for (iVar = 0; iVar < pIntEnv->cVars; iVar++)
            if (!pIntEnv->pfnCompare(pIntEnv->papszEnv[iVar], pszVar, cchVar))
            {
                if (pIntEnv->papszEnv[iVar][cchVar] == '=')
                {
                    rc = VINF_SUCCESS;
                    const char *pszValueOrg = pIntEnv->papszEnv[iVar] + cchVar + 1;
                    size_t cch = strlen(pszValueOrg);
                    if (pcchActual)
                        *pcchActual = cch;
                    if (pszValue && cbValue)
                    {
                        if (cch < cbValue)
                            memcpy(pszValue, pszValueOrg, cch + 1);
                        else
                            rc = VERR_BUFFER_OVERFLOW;
                    }
                    break;
                }
                if (pIntEnv->papszEnv[iVar][cchVar] == '\0')
                {
                    Assert(pIntEnv->fPutEnvBlock);
                    rc = VERR_ENV_VAR_UNSET;
                    break;
                }
            }

        RTENV_UNLOCK(pIntEnv);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTEnvGetEx);


RTDECL(bool) RTEnvExistEx(RTENV Env, const char *pszVar)
{
    AssertPtrReturn(pszVar, false);

    bool fExists = false;
    if (Env == RTENV_DEFAULT)
    {
#ifdef RTENV_IMPLEMENTS_UTF8_DEFAULT_ENV_API
        fExists = RTEnvExistsUtf8(pszVar);
#else
        /*
         * Since RTEnvExist isn't UTF-8 clean and actually expects the strings
         * to be in the current code page (codeset), we'll do the necessary
         * conversions here.
         */
        char *pszVarOtherCP;
        int rc = RTStrUtf8ToCurrentCP(&pszVarOtherCP, pszVar);
        if (RT_SUCCESS(rc))
        {
            fExists = RTEnvExist(pszVarOtherCP);
            RTStrFree(pszVarOtherCP);
        }
#endif
    }
    else
    {
        PRTENVINTERNAL pIntEnv = Env;
        AssertPtrReturn(pIntEnv, false);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, false);
        const size_t cchVar = strlen(pszVar);
        AssertReturn(cchVar > 0, false);
        AssertReturn(strchr(pIntEnv->fFirstEqual ? pszVar + 1 : pszVar, '=') == NULL, false);

        RTENV_LOCK(pIntEnv);

        /*
         * Simple search.
         */
        for (size_t iVar = 0; iVar < pIntEnv->cVars; iVar++)
            if (!pIntEnv->pfnCompare(pIntEnv->papszEnv[iVar], pszVar, cchVar))
            {
                if (pIntEnv->papszEnv[iVar][cchVar] == '=')
                {
                    fExists = true;
                    break;
                }
                if (pIntEnv->papszEnv[iVar][cchVar] == '\0')
                    break;
            }

        RTENV_UNLOCK(pIntEnv);
    }
    return fExists;
}
RT_EXPORT_SYMBOL(RTEnvExistEx);


#ifndef RT_OS_WINDOWS
RTDECL(char const * const *) RTEnvGetExecEnvP(RTENV Env)
{
    const char * const *papszRet;
    if (Env == RTENV_DEFAULT)
    {
        /** @todo fix this API it's fundamentally wrong! */
        papszRet = rtEnvDefault();
        if (!papszRet)
        {
            static const char * const s_papszDummy[2] = { NULL, NULL };
            papszRet = &s_papszDummy[0];
        }
    }
    else
    {
        PRTENVINTERNAL pIntEnv = Env;
        AssertPtrReturn(pIntEnv, NULL);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, NULL);

        RTENV_LOCK(pIntEnv);

        /*
         * Free any old envp.
         */
        if (pIntEnv->papszEnvOtherCP)
        {
            for (size_t iVar = 0; pIntEnv->papszEnvOtherCP[iVar]; iVar++)
            {
                RTStrFree(pIntEnv->papszEnvOtherCP[iVar]);
                pIntEnv->papszEnvOtherCP[iVar] = NULL;
            }
            RTMemFree(pIntEnv->papszEnvOtherCP);
            pIntEnv->papszEnvOtherCP = NULL;
        }

        /*
         * Construct a new envp with the strings in the process code set.
         */
        char **papsz;
        papszRet = pIntEnv->papszEnvOtherCP = papsz = (char **)RTMemAlloc(sizeof(char *) * (pIntEnv->cVars + 1));
        if (papsz)
        {
            papsz[pIntEnv->cVars] = NULL;
            for (size_t iVar = 0; iVar < pIntEnv->cVars; iVar++)
            {
                int rc = RTStrUtf8ToCurrentCP(&papsz[iVar], pIntEnv->papszEnv[iVar]);
                if (RT_FAILURE(rc))
                {
                    /* RTEnvDestroy / we cleans up later. */
                    papsz[iVar] = NULL;
                    AssertRC(rc);
                    papszRet = NULL;
                    break;
                }
            }
        }

        RTENV_UNLOCK(pIntEnv);
    }
    return papszRet;
}
RT_EXPORT_SYMBOL(RTEnvGetExecEnvP);
#endif /* !RT_OS_WINDOWS */


/**
 * RTSort callback for comparing two environment variables.
 *
 * @returns -1, 0, 1. See PFNRTSORTCMP.
 * @param   pvElement1          Variable 1.
 * @param   pvElement2          Variable 2.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int) rtEnvSortCompare(const void *pvElement1, const void *pvElement2, void *pvUser)
{
    NOREF(pvUser);
    int iDiff = strcmp((const char *)pvElement1, (const char *)pvElement2);
    if (iDiff < 0)
        iDiff = -1;
    else if (iDiff > 0)
        iDiff = 1;
    return iDiff;
}


RTDECL(int) RTEnvQueryUtf16Block(RTENV hEnv, PRTUTF16 *ppwszzBlock)
{
    RTENV           hClone  = NIL_RTENV;
    PRTENVINTERNAL  pIntEnv;
    int             rc;

    /*
     * Validate / simplify input.
     */
    if (hEnv == RTENV_DEFAULT)
    {
        rc = RTEnvClone(&hClone, RTENV_DEFAULT);
        if (RT_FAILURE(rc))
            return rc;
        pIntEnv = hClone;
    }
    else
    {
        pIntEnv = hEnv;
        AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);
        rc = VINF_SUCCESS;
    }

    RTENV_LOCK(pIntEnv);

    /*
     * Sort it first.
     */
    RTSortApvShell((void **)pIntEnv->papszEnv, pIntEnv->cVars, rtEnvSortCompare, pIntEnv);

    /*
     * Calculate the size.
     */
    size_t cwc;
    size_t cwcTotal = 2;
    for (size_t iVar = 0; iVar < pIntEnv->cVars; iVar++)
    {
        rc = RTStrCalcUtf16LenEx(pIntEnv->papszEnv[iVar], RTSTR_MAX, &cwc);
        AssertRCBreak(rc);
        cwcTotal += cwc + 1;
    }

    PRTUTF16 pwszzBlock = NULL;
    if (RT_SUCCESS(rc))
    {
        /*
         * Perform the conversion.
         */
        PRTUTF16 pwszz = pwszzBlock = (PRTUTF16)RTMemAlloc(cwcTotal * sizeof(RTUTF16));
        if (pwszz)
        {
            size_t cwcLeft = cwcTotal;
            for (size_t iVar = 0; iVar < pIntEnv->cVars; iVar++)
            {
                rc = RTStrToUtf16Ex(pIntEnv->papszEnv[iVar], RTSTR_MAX,
                                    &pwszz, cwcTotal - (pwszz - pwszzBlock), &cwc);
                AssertRCBreak(rc);
                pwszz   += cwc + 1;
                cwcLeft -= cwc + 1;
                AssertBreakStmt(cwcLeft >= 2, rc = VERR_INTERNAL_ERROR_3);
            }
            AssertStmt(cwcLeft == 2 || RT_FAILURE(rc), rc = VERR_INTERNAL_ERROR_2);
            if (RT_SUCCESS(rc))
            {
                pwszz[0] = '\0';
                pwszz[1] = '\0';
            }
            else
            {
                RTMemFree(pwszzBlock);
                pwszzBlock = NULL;
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }

    RTENV_UNLOCK(pIntEnv);

    if (hClone != NIL_RTENV)
        RTEnvDestroy(hClone);
    if (RT_SUCCESS(rc))
        *ppwszzBlock = pwszzBlock;
    return rc;
}
RT_EXPORT_SYMBOL(RTEnvQueryUtf16Block);


RTDECL(void) RTEnvFreeUtf16Block(PRTUTF16 pwszzBlock)
{
    RTMemFree(pwszzBlock);
}
RT_EXPORT_SYMBOL(RTEnvFreeUtf16Block);


RTDECL(int) RTEnvQueryUtf8Block(RTENV hEnv, bool fSorted, char **ppszzBlock, size_t *pcbBlock)
{
    RTENV           hClone  = NIL_RTENV;
    PRTENVINTERNAL  pIntEnv;
    int             rc;

    /*
     * Validate / simplify input.
     */
    if (hEnv == RTENV_DEFAULT)
    {
        rc = RTEnvClone(&hClone, RTENV_DEFAULT);
        if (RT_FAILURE(rc))
            return rc;
        pIntEnv = hClone;
    }
    else
    {
        pIntEnv = hEnv;
        AssertPtrReturn(pIntEnv, VERR_INVALID_HANDLE);
        AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);
        rc = VINF_SUCCESS;
    }

    RTENV_LOCK(pIntEnv);

    /*
     * Sort it, if requested.
     */
    if (fSorted)
        RTSortApvShell((void **)pIntEnv->papszEnv, pIntEnv->cVars, rtEnvSortCompare, pIntEnv);

    /*
     * Calculate the size. We add one extra terminator just to be on the safe side.
     */
    size_t cbBlock = 2;
    for (size_t iVar = 0; iVar < pIntEnv->cVars; iVar++)
        cbBlock += strlen(pIntEnv->papszEnv[iVar]) + 1;

    if (pcbBlock)
        *pcbBlock = cbBlock - 1;

    /*
     * Allocate memory and copy out the variables.
     */
    char *pszzBlock;
    char *pszz = pszzBlock = (char *)RTMemAlloc(cbBlock);
    if (pszz)
    {
        size_t cbLeft = cbBlock;
        for (size_t iVar = 0; iVar < pIntEnv->cVars; iVar++)
        {
            size_t cb = strlen(pIntEnv->papszEnv[iVar]) + 1;
            AssertBreakStmt(cb + 2 <= cbLeft, rc = VERR_INTERNAL_ERROR_3);
            memcpy(pszz, pIntEnv->papszEnv[iVar], cb);
            pszz   += cb;
            cbLeft -= cb;
        }
        if (RT_SUCCESS(rc))
        {
            pszz[0] = '\0';
            pszz[1] = '\0'; /* The extra one. */
        }
        else
        {
            RTMemFree(pszzBlock);
            pszzBlock = NULL;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    RTENV_UNLOCK(pIntEnv);

    if (hClone != NIL_RTENV)
        RTEnvDestroy(hClone);
    if (RT_SUCCESS(rc))
        *ppszzBlock = pszzBlock;
    return rc;
}
RT_EXPORT_SYMBOL(RTEnvQueryUtf8Block);


RTDECL(void) RTEnvFreeUtf8Block(char *pszzBlock)
{
    RTMemFree(pszzBlock);
}
RT_EXPORT_SYMBOL(RTEnvFreeUtf8Block);


RTDECL(uint32_t) RTEnvCountEx(RTENV hEnv)
{
    PRTENVINTERNAL pIntEnv = hEnv;
    AssertPtrReturn(pIntEnv, UINT32_MAX);
    AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, UINT32_MAX);

    RTENV_LOCK(pIntEnv);
    uint32_t cVars = (uint32_t)pIntEnv->cVars;
    RTENV_UNLOCK(pIntEnv);

    return cVars;
}
RT_EXPORT_SYMBOL(RTEnvCountEx);


RTDECL(int) RTEnvGetByIndexEx(RTENV hEnv, uint32_t iVar, char *pszVar, size_t cbVar, char *pszValue, size_t cbValue)
{
    PRTENVINTERNAL pIntEnv = hEnv;
    AssertPtrReturn(pIntEnv, UINT32_MAX);
    AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, UINT32_MAX);
    if (cbVar)
        AssertPtrReturn(pszVar, VERR_INVALID_POINTER);
    if (cbValue)
        AssertPtrReturn(pszValue, VERR_INVALID_POINTER);

    RTENV_LOCK(pIntEnv);

    int rc;
    if (iVar < pIntEnv->cVars)
    {
        const char *pszSrcVar   = pIntEnv->papszEnv[iVar];
        const char *pszSrcValue = strchr(pszSrcVar, '=');
        if (pszSrcValue == pszSrcVar && pIntEnv->fFirstEqual)
            pszSrcValue = strchr(pszSrcVar + 1, '=');
        bool        fHasEqual   = pszSrcValue != NULL;
        if (pszSrcValue)
        {
            pszSrcValue++;
            rc = VINF_SUCCESS;
        }
        else
        {
            pszSrcValue = strchr(pszSrcVar, '\0');
            rc = VINF_ENV_VAR_UNSET;
        }
        if (cbVar)
        {
            int rc2 = RTStrCopyEx(pszVar, cbVar, pszSrcVar, pszSrcValue - pszSrcVar - fHasEqual);
            if (RT_FAILURE(rc2))
                rc = rc2;
        }
        if (cbValue)
        {
            int rc2 = RTStrCopy(pszValue, cbValue, pszSrcValue);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
        rc = VERR_ENV_VAR_NOT_FOUND;

    RTENV_UNLOCK(pIntEnv);

    return rc;
}
RT_EXPORT_SYMBOL(RTEnvGetByIndexEx);


RTDECL(const char *) RTEnvGetByIndexRawEx(RTENV hEnv, uint32_t iVar)
{
    PRTENVINTERNAL pIntEnv = hEnv;
    AssertPtrReturn(pIntEnv, NULL);
    AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, NULL);

    RTENV_LOCK(pIntEnv);

    const char *pszRet;
    if (iVar < pIntEnv->cVars)
        pszRet = pIntEnv->papszEnv[iVar];
    else
        pszRet = NULL;

    RTENV_UNLOCK(pIntEnv);

    return pszRet;
}
RT_EXPORT_SYMBOL(RTEnvGetByIndexRawEx);


RTDECL(int) RTEnvCreateChangeRecord(PRTENV phEnv)
{
    AssertPtrReturn(phEnv, VERR_INVALID_POINTER);
#ifdef RTENV_ALLOW_EQUAL_FIRST_IN_VAR
    return rtEnvCreate(phEnv, RTENV_GROW_SIZE, true /*fCaseSensitive*/, true /*fPutEnvBlock*/, true /*fFirstEqual*/);
#else
    return rtEnvCreate(phEnv, RTENV_GROW_SIZE, true /*fCaseSensitive*/, true /*fPutEnvBlock*/, false /*fFirstEqual*/);
#endif
}
RT_EXPORT_SYMBOL(RTEnvCreateChangeRecord);


RTDECL(int) RTEnvCreateChangeRecordEx(PRTENV phEnv, uint32_t fFlags)
{
    AssertPtrReturn(phEnv, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTENV_CREATE_F_VALID_MASK), VERR_INVALID_FLAGS);
    return rtEnvCreate(phEnv, RTENV_GROW_SIZE, true /*fCaseSensitive*/, true /*fPutEnvBlock*/,
                       RT_BOOL(fFlags & RTENV_CREATE_F_ALLOW_EQUAL_FIRST_IN_VAR));
}
RT_EXPORT_SYMBOL(RTEnvCreateChangeRecord);


RTDECL(bool) RTEnvIsChangeRecord(RTENV hEnv)
{
    if (hEnv == RTENV_DEFAULT)
        return false;

    PRTENVINTERNAL pIntEnv = hEnv;
    AssertPtrReturn(pIntEnv, false);
    AssertReturn(pIntEnv->u32Magic == RTENV_MAGIC, false);
    return pIntEnv->fPutEnvBlock;
}
RT_EXPORT_SYMBOL(RTEnvIsChangeRecord);


RTDECL(int) RTEnvApplyChanges(RTENV hEnvDst, RTENV hEnvChanges)
{
    PRTENVINTERNAL pIntEnvChanges = hEnvChanges;
    AssertPtrReturn(pIntEnvChanges, VERR_INVALID_HANDLE);
    AssertReturn(pIntEnvChanges->u32Magic == RTENV_MAGIC, VERR_INVALID_HANDLE);

    /** @todo lock validator trouble ahead here! */
    RTENV_LOCK(pIntEnvChanges);

    int rc = VINF_SUCCESS;
    for (uint32_t iChange = 0; iChange < pIntEnvChanges->cVars && RT_SUCCESS(rc); iChange++)
        rc = RTEnvPutEx(hEnvDst, pIntEnvChanges->papszEnv[iChange]);

    RTENV_UNLOCK(pIntEnvChanges);

    return rc;
}
RT_EXPORT_SYMBOL(RTEnvApplyChanges);

