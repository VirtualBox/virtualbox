/* $Id: pathhost-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path Conversions, POSIX.
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
#define LOG_GROUP RTLOGGROUP_PATH
#include "internal/iprt.h"
#include "internal/path.h"
#include "internal/string.h"
#include "internal/thread.h"

#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/once.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Initialize once object. */
static RTONCE       g_OnceInitPathConv = RTONCE_INITIALIZER;
/** If set, then we can pass UTF-8 thru directly. */
static bool         g_fPassthruUtf8    = false;
/** The UTF-8 to FS iconv cache entry. */
static RTSTRICONV   g_enmUtf8ToFsIdx   = RTSTRICONV_UTF8_TO_LOCALE;
/** The FS to UTF-8 iconv cache entry. */
static RTSTRICONV   g_enmFsToUtf8Idx   = RTSTRICONV_LOCALE_TO_UTF8;
/** The codeset we're using. */
static char         g_szFsCodeset[32];


/**
 * Do a case insensitive compare where the 2nd string is known and can be case
 * folded when writing the code.
 *
 * @returns see strcmp.
 * @param   pszStr1             The string to compare against pszLower and
 *                              pszUpper.
 * @param   pszUpper            The upper case edition of the 2nd string.
 * @param   pszLower            The lower case edition of the 2nd string.
 */
static int rtPathStrICmp(const char *pszStr1, const char *pszUpper, const char *pszLower)
{
    Assert(strlen(pszLower) == strlen(pszUpper));
    for (;;)
    {
        char ch1      = *pszStr1++;
        char ch2Upper = *pszUpper++;
        char ch2Lower = *pszLower++;
        if (   ch1 != ch2Upper
            && ch1 != ch2Lower)
            return ch1 < ch2Upper ? -1 : 1;
        if (!ch1)
            return 0;
    }
}

/**
 * Is the specified codeset something we can treat as UTF-8.
 *
 * @returns true if we can do UTF-8 passthru, false if not.
 * @param   pszCodeset          The codeset in question.
 */
static bool rtPathConvInitIsUtf8(const char *pszCodeset)
{
    /* Paranoia. */
    if (!pszCodeset)
        return false;

    /*
     * Avoid RTStrICmp at this point.
     */
    static struct
    {
        const char *pszUpper;
        const char *pszLower;
    } const s_aUtf8Compatible[] =
    {
        /* The default locale. */
        { "C"                   , "c"                   },
        { "POSIX"               , "posix"               },
        /* 7-bit ASCII. */
        { "ANSI_X3.4-1968"      , "ansi_x3.4-1968"      },
        { "ANSI_X3.4-1986"      , "ansi_x3.4-1986"      },
        { "US-ASCII"            , "us-ascii"            },
        { "ISO646-US"           , "iso646-us"           },
        { "ISO_646.IRV:1991"    , "iso_646.irv:1991"    },
        { "ISO-IR-6"            , "iso-ir-6"            },
        { "IBM367"              , "ibm367"              },
        /* UTF-8 */
        { "UTF-8"               , "utf-8"               },
        { "UTF8"                , "utf8"                },
        { "ISO-10646/UTF-8"     , "iso-10646/utf-8"     },
        { "ISO-10646/UTF8"      , "iso-10646/utf8"      }
    };

    for (size_t i = 0; i < RT_ELEMENTS(s_aUtf8Compatible); i++)
        if (!rtPathStrICmp(pszCodeset, s_aUtf8Compatible[i].pszUpper, s_aUtf8Compatible[i].pszLower))
            return true;

    return false;
}


/**
 * Init once for the path conversion code.
 *
 * @returns IPRT status code.
 * @param   pvUser1             Unused.
 * @param   pvUser2             Unused.
 */
static DECLCALLBACK(int32_t) rtPathConvInitOnce(void *pvUser)
{
    /*
     * Read the environment variable, no mercy on misconfigs here except that
     * empty values are quietly ignored.  (We use a temp buffer for stripping.)
     */
    char *pszEnvValue = NULL;
    char  szEnvValue[sizeof(g_szFsCodeset)];
    int rc = RTEnvGetEx(RTENV_DEFAULT, RTPATH_CODESET_ENV_VAR, szEnvValue, sizeof(szEnvValue), NULL);
    if (rc != VERR_ENV_VAR_NOT_FOUND && RT_FAILURE(rc))
        return rc;
    if (RT_SUCCESS(rc))
        pszEnvValue = RTStrStrip(szEnvValue);

    if (pszEnvValue && *pszEnvValue)
    {
        g_fPassthruUtf8  = rtPathConvInitIsUtf8(pszEnvValue);
        g_enmFsToUtf8Idx = RTSTRICONV_FS_TO_UTF8;
        g_enmUtf8ToFsIdx = RTSTRICONV_UTF8_TO_FS;
        strcpy(g_szFsCodeset, pszEnvValue);
    }
    else
    {
        const char *pszCodeset = rtStrGetLocaleCodeset();
        size_t      cchCodeset = pszCodeset ? strlen(pszCodeset) : sizeof(g_szFsCodeset);
        if (cchCodeset >= sizeof(g_szFsCodeset))
            /* This shouldn't happen, but we'll manage. */
            g_szFsCodeset[0] = '\0';
        else
        {
            memcpy(g_szFsCodeset, pszCodeset, cchCodeset + 1);
            pszCodeset = g_szFsCodeset;
        }
        g_fPassthruUtf8  = rtPathConvInitIsUtf8(pszCodeset);
        g_enmFsToUtf8Idx = RTSTRICONV_LOCALE_TO_UTF8;
        g_enmUtf8ToFsIdx = RTSTRICONV_UTF8_TO_LOCALE;
    }

    NOREF(pvUser);
    return VINF_SUCCESS;
}


int rtPathToNative(char const **ppszNativePath, const char *pszPath, const char *pszBasePath)
{
    *ppszNativePath = NULL;

    int rc = RTOnce(&g_OnceInitPathConv, rtPathConvInitOnce, NULL);
    if (RT_SUCCESS(rc))
    {
        if (g_fPassthruUtf8 || !*pszPath)
            *ppszNativePath = pszPath;
        else
            rc = rtStrConvert(pszPath, strlen(pszPath), "UTF-8",
                              (char **)ppszNativePath, 0, g_szFsCodeset,
                              2, g_enmUtf8ToFsIdx);
    }
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}


void rtPathFreeNative(char const *pszNativePath, const char *pszPath)
{
    if (    pszNativePath != pszPath
        &&  pszNativePath)
        RTStrFree((char *)pszNativePath);
}


int rtPathFromNative(const char **ppszPath, const char *pszNativePath, const char *pszBasePath)
{
    *ppszPath = NULL;

    int rc = RTOnce(&g_OnceInitPathConv, rtPathConvInitOnce, NULL);
    if (RT_SUCCESS(rc))
    {
        if (g_fPassthruUtf8 || !*pszNativePath)
        {
            size_t cCpsIgnored;
            size_t cchNativePath;
            rc = rtUtf8Length(pszNativePath, RTSTR_MAX, &cCpsIgnored, &cchNativePath);
            if (RT_SUCCESS(rc))
            {
                char *pszPath;
                *ppszPath = pszPath = RTStrAlloc(cchNativePath + 1);
                if (pszPath)
                    memcpy(pszPath, pszNativePath, cchNativePath + 1);
                else
                    rc = VERR_NO_STR_MEMORY;
            }
        }
        else
            rc = rtStrConvert(pszNativePath, strlen(pszNativePath), g_szFsCodeset,
                              (char **)ppszPath, 0, "UTF-8",
                              2, g_enmFsToUtf8Idx);
    }
    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}


void rtPathFreeIprt(const char *pszPath, const char *pszNativePath)
{
    if (   pszPath != pszNativePath
        && pszPath)
        RTStrFree((char *)pszPath);
}


int rtPathFromNativeCopy(char *pszPath, size_t cbPath, const char *pszNativePath, const char *pszBasePath)
{
    int rc = RTOnce(&g_OnceInitPathConv, rtPathConvInitOnce, NULL);
    if (RT_SUCCESS(rc))
    {
        if (g_fPassthruUtf8 || !*pszNativePath)
            rc = RTStrCopy(pszPath, cbPath, pszNativePath);
        else if (cbPath)
            rc = rtStrConvert(pszNativePath, strlen(pszNativePath), g_szFsCodeset,
                              &pszPath, cbPath, "UTF-8",
                              2, g_enmFsToUtf8Idx);
        else
            rc = VERR_BUFFER_OVERFLOW;
    }

    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}


int rtPathFromNativeDup(char **ppszPath, const char *pszNativePath, const char *pszBasePath)
{
    int rc = RTOnce(&g_OnceInitPathConv, rtPathConvInitOnce, NULL);
    if (RT_SUCCESS(rc))
    {
        if (g_fPassthruUtf8 || !*pszNativePath)
            rc = RTStrDupEx(ppszPath, pszNativePath);
        else
            rc = rtStrConvert(pszNativePath, strlen(pszNativePath), g_szFsCodeset,
                              ppszPath, 0, "UTF-8",
                              2, g_enmFsToUtf8Idx);
    }

    NOREF(pszBasePath); /* We don't query the FS for codeset preferences. */
    return rc;
}

