/** @file
 * libslirp: glib replacement header
 */

/*
 * Copyright (C) 2006-2023 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_include_glib_h
#define VBOX_INCLUDED_SRC_include_glib_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef LOG_GROUP
# define LOG_GROUP LOG_GROUP_NAT_ENGINE
#endif
#include <VBox/log.h>

#include <iprt/asm.h>       /* for RT_BE2H_XXX and RT_H2BE_XXX */
#include <iprt/assert.h>
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/stdarg.h>
#include <iprt/string.h>
#include <iprt/rand.h>
#include <iprt/handletable.h>

#if !defined(RT_OS_WINDOWS)
# include <signal.h>
# define G_OS_UNIX
#else
# define G_OS_WIN32
#endif

#define G_LITTLE_ENDIAN 1234
#define G_BIG_ENDIAN    4321
#define GLIB_SIZEOF_VOID_P 8

#ifndef RT_BIG_ENDIAN
# define G_BYTE_ORDER G_LITTLE_ENDIAN
#else
# define G_BYTE_ORDER G_BIG_ENDIAN
#endif

/** @name Typedefs
 * @{ */
typedef char    gchar;
typedef bool    gboolean;
typedef int     gint;
typedef int     GPid;
typedef void   *gpointer;
typedef int     GRand;
typedef void   *GSpawnChildSetupFunc;
typedef int     GSpawnFlags;
typedef char  **GStrv;
/** @} */

/** @name Constants
 * @{ */
#define STR_LEN_MAX             4096
#define G_SPAWN_SEARCH_PATH     4
#undef FALSE                                /* windows.h  */
#define FALSE                   false
#undef TRUE                                 /* windows.h */
#define TRUE                    true
/* #} */

/** @name Data Structures
 * @{ */

typedef struct GError
{
    uint32_t    domain;
    int         code;
    char       *message;
} GError;

typedef struct GString
{
    char       *str;
    size_t      len;
    size_t      cbAlloc;
} GString;

/** @} */

#define g_assert(x)                     Assert(x)
#define g_assert_not_reached()          AssertFatalFailed()

DECLINLINE(void) g_critical(const char *pszFmt, ...)
{
    va_list args;
    va_start(args, pszFmt);
    RTLogRelPrintf("%N", pszFmt, &args);
    va_end(args);
#ifdef LOG_ENABLED
    va_start(args, pszFmt);
    Log(("%N", pszFmt, &args));
    va_end(args);
#endif
}

#define g_error(...)                    LogRel((__VA_ARGS__))
#define g_warning(...)                  LogRelWarn((__VA_ARGS__))

#define g_warn_if_fail(x) do { \
        if (RT_LIKELY(!x)) { /* likely */ } \
        else g_warning("WARNING\n"); \
    } while (0)

#define g_warn_if_reached()            g_warning("WARNING: function %s line %s", __PRETTY_FUNCTION__, __LINE__)

#define g_return_if_fail(a_Expr)            do { if (RT_LIKELY((a_Expr))) {/* likely */} else return; } while (0)

#define g_return_val_if_fail(a_Expr, a_rc)  do { if (RT_LIKELY((a_Expr))) {/* likely */} else return (a_rc); } while (0)

#define G_STATIC_ASSERT(x)              AssertCompile(x)


/** @name Memory Allocation
 * @{ */

#define g_free(x)                       RTMemFree(x)
#define g_malloc(x)                     RTMemAlloc(x)
#define g_new(x, y)                     RTMemAlloc(sizeof(x) * (y))
#define g_new0(x, y)                    RTMemAllocZ(sizeof(x) * (y))
#define g_malloc0(x)                    RTMemAllocZ(x)
#define g_realloc(x, y)                 RTMemRealloc(x, y)

DECLINLINE(GError *) g_error_new(uint32_t uDomain, int iErr, const char *pszMsg)
{
    size_t cbMsg = pszMsg ? strlen(pszMsg) + 1 : 0;
    GError *pErr = (GError *)g_malloc(sizeof(*pErr) + cbMsg);
    if (pErr)
    {
        pErr->domain  = uDomain;
        pErr->code    = iErr;
        pErr->message = pszMsg ? (char *)memcpy(pErr + 1, pszMsg, cbMsg) : NULL;
    }
    return pErr;
}

DECLINLINE(void) g_error_free(GError *pErr)
{
    g_free(pErr);
}

DECLINLINE(char *) g_strdup(const char *pcszStr)
{
    if (pcszStr != NULL)
        return RTStrDup(pcszStr);
    return NULL;
}

/** @} */

/** @name String Functions
 * @{ */
#ifdef __GNUC__
# define G_GNUC_PRINTF(x, y)             __attribute__((format (printf, x, y)))
#else
# define G_GNUC_PRINTF(x, y)
#endif

DECLINLINE(int) g_vsnprintf(char *pszDst, size_t cbDst, const char *pszFmt, va_list va)
{
    ssize_t cchRet = RTStrPrintf2V(pszDst, cbDst, pszFmt, va);
    return (int)(cchRet >= 0 ? cchRet : -cchRet);
}

DECLINLINE(int) g_snprintf(char *pszDst, size_t cbDst, const char *pszFmt, ...)
{
    va_list va;
    va_start(va, pszFmt);
    int cchRet = g_vsnprintf(pszDst, cbDst, pszFmt, va);
    va_end(va);
    return cchRet;
}

DECLINLINE(GString *) g_string_new(const char *pszInitial)
{
    GString *pGStr = (GString *)RTMemAlloc(sizeof(GString));
    if (pGStr)
    {
        size_t const cchSrc  = pszInitial ? strlen(pszInitial) : 0;
        size_t const cbAlloc = RT_ALIGN_Z(cchSrc + 1, 64);
        pGStr->str = RTStrAlloc(cbAlloc);
        AssertReturnStmt(pGStr->str, RTMemFree(pGStr), NULL);

        if (pszInitial)
            memcpy(pGStr->str, pszInitial, cchSrc);
        pGStr->str[cchSrc] = '\0';
        pGStr->len     = cchSrc;
        pGStr->cbAlloc = cbAlloc;
    }

    return pGStr;
}

DECLINLINE(char *) g_string_free(GString *pGStr, bool fFreeCString)
{
    char *pszCString = pGStr->str;
    pGStr->str     = NULL;
    pGStr->len     = 0;
    pGStr->cbAlloc = 0;
    RTMemFree(pGStr);
    if (fFreeCString)
    {
        RTStrFree(pszCString);
        pszCString = NULL;
    }
    return pszCString;
}

DECLINLINE(GString *) g_string_append_len(GString *pGStr, const char *pszSrc, size_t cchSrc)
{
    size_t cchDst = pGStr->len;
    if (cchDst + cchSrc >= pGStr->cbAlloc)
    {
        size_t const cbAlloc = RT_ALIGN_Z(cchDst + cchSrc + 16, 64);
        int rc = RTStrRealloc(&pGStr->str, cbAlloc);
        AssertRCReturn(RT_SUCCESS(rc), NULL);
        pGStr->cbAlloc = cbAlloc;
    }
    memcpy(&pGStr->str[cchDst], pszSrc, cchSrc);
    cchDst += cchSrc;
    pGStr->str[cchDst] = '\0';
    pGStr->len = cchDst;
    return pGStr;
}


DECLINLINE(GString *) g_string_append_printf(GString *pGStr, const char *pszFmt, ...)
{
    va_list args;
    va_start(args, pszFmt);
    char *pszTmp = NULL;
    ssize_t cchRet = RTStrAPrintfV(&pszTmp, pszFmt, args);
    va_end(args);
    AssertReturn(cchRet >= 0, NULL);

    pGStr = g_string_append_len(pGStr, pszTmp, (size_t)cchRet);
    RTStrFree(pszTmp);
    return pGStr;
}

DECLINLINE(char *) g_strstr_len(const char *pszHaystack, ssize_t cchHaystack, const char *pszNeedle)
{
    if (cchHaystack != -1)
    {
        size_t const cchNeedle = strlen(pszNeedle);
        if (cchHaystack >= (ssize_t)cchNeedle)
        {
            char const ch0Needle = *pszNeedle;
            cchHaystack -= cchNeedle - 1;
            while (cchHaystack > 0)
            {
                const char *pszHit = (const char *)memchr(pszHaystack, ch0Needle, cchHaystack);
                if (!pszHit)
                    return NULL;
                if (memcmp(pszHit, pszNeedle, cchNeedle) == 0)
                    return (char *)pszHit;
                cchHaystack -= (ssize_t)(&pszHit[1] - pszHaystack);
                pszHaystack  = &pszHit[1];
            }
        }
        return NULL;
    }
    return RTStrStr(pszHaystack, pszNeedle);
}

DECLINLINE(bool) g_str_has_prefix(const char *pcszStr, const char *pcszPrefix)
{
    return RTStrStartsWith(pcszStr, pcszPrefix);
}

DECLINLINE(void) g_strfreev(char **papszStrings)
{
    if (papszStrings)
    {
        size_t i = 0;
        char *pszEntry;
        while ((pszEntry = papszStrings[i]) != NULL)
        {
            RTStrFree(pszEntry);
            papszStrings[i] = NULL;
            i++;
        }
        RTMemFree(papszStrings);
    }
}

DECLINLINE(size_t) g_strlcpy(char *pszDst, const char *pszSrc, size_t cchSrc)
{
    return RTStrCopy(pszDst, cchSrc, pszSrc);
}

DECLINLINE(unsigned) g_strv_length(char **papszStrings)
{
    unsigned cStrings = 0;
    AssertPtr(papszStrings);
    if (papszStrings)
        while (*papszStrings++ != NULL)
            cStrings++;
    return cStrings;
}

/** This is only used once to report a g_vsnprintf error for which errno (our x)
 *  isn't even set. */
#define g_strerror(x)                   ("whatever")

/** @} */

/** @name Misc. Functions
 * @{ */
#define GLIB_CHECK_VERSION(x, y, z)     true

#define GINT16_FROM_BE(x)               RT_BE2H_S16(x)
#define GINT16_TO_BE(x)                 RT_H2BE_S16(x)
#define GINT32_FROM_BE(x)               RT_BE2H_S32(x)
#define GINT32_TO_BE(x)                 RT_H2BE_S32(x)

#define GUINT16_FROM_BE(x)              RT_BE2H_U16(x)
#define GUINT16_TO_BE(x)                RT_H2BE_U16(x)
#define GUINT32_FROM_BE(x)              RT_BE2H_U32(x)
#define GUINT32_TO_BE(x)                RT_H2BE_U32(x)

#define MAX(x, y)                       RT_MAX(x, y)
#define MIN(x, y)                       RT_MIN(x, y)
#define G_N_ELEMENTS(x)                 RT_ELEMENTS(x)
#define G_LIKELY(x)                     RT_LIKELY(x)
#define G_UNLIKELY(x)                   RT_UNLIKELY(x)

#define g_rand_int_range(a_hRand, a_uFirst, a_uEnd) RTRandS32Ex(a_uFirst, (a_uEnd) - 1)
#define g_rand_new()                    NULL
#define g_rand_free(a_hRand)            ((void)0)

DECLINLINE(bool) g_shell_parse_argv(const char *pszCmdLine, int *pcArgs, char ***ppapszArgs, GError **ppErr)
{
    char **papszTmpArgs = NULL;
    int    cTmpArgs     = 0;
    int rc = RTGetOptArgvFromString(&papszTmpArgs, &cTmpArgs, pszCmdLine, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH, NULL /*pszIfs*/);
    if (RT_SUCCESS(rc))
    {
        /* Must create a regular string array compatible with g_strfreev.  */
        char **papszArgs = (char **)RTMemAllocZ(sizeof(char *) * (cTmpArgs + 1));
        if (papszArgs)
        {
            int iArg = 0;
            while (iArg < cTmpArgs)
            {
                papszArgs[iArg] = RTStrDup(papszTmpArgs[iArg]);
                AssertBreak(papszArgs[iArg]);
                iArg++;
            }
            if (iArg == cTmpArgs)
            {
                papszArgs[iArg] = NULL;

                *ppapszArgs = papszArgs;
                *pcArgs     = iArg;
                if (ppErr)
                    *ppErr = NULL;
                RTGetOptArgvFree(papszTmpArgs);
                return true;
            }

            while (iArg-- > 0)
                RTStrFree(papszArgs[iArg]);
            RTMemFree(papszArgs);
        }
        RTGetOptArgvFree(papszTmpArgs);
        if (ppErr)
            *ppErr = g_error_new(0, VERR_NO_STR_MEMORY, "VERR_NO_STR_MEMORY");
    }
    else if (ppErr)
        *ppErr = g_error_new(0, rc, "RTGetOptArgvFromString failed");
    return false;
}

DECLINLINE(bool) g_spawn_async_with_fds(const char *working_directory, char **argv,
                                        char **envp, int flags,
                                        GSpawnChildSetupFunc child_setup,
                                        void * user_data, GPid *child_pid, int stdin_fd,
                                        int stdout_fd, int stderr_fd, GError **error)
{
    /** @todo r=bird: This is best replaced directly in the src/misc.c file as
     *        there we know exactly what stdin_fd, stdout_fd and stderr_fd are
     *        can can do a better mapping to RTProcCreateEx. */
    AssertFailed();
    return false;
}


/** @} */

#endif /* !VBOX_INCLUDED_SRC_include_glib_h */
