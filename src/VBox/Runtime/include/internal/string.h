/* $Id: string.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal RTStr header.
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

#ifndef IPRT_INCLUDED_INTERNAL_string_h
#define IPRT_INCLUDED_INTERNAL_string_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/string.h>

RT_C_DECLS_BEGIN

/** @def RTSTR_STRICT
 * Enables strict assertions on bad string encodings.
 */
#ifdef DOXYGEN_RUNNING
# define RTSTR_STRICT
#endif
/*#define RTSTR_STRICT*/

#ifdef RTSTR_STRICT
# define RTStrAssertMsgFailed(msg)              AssertMsgFailed(msg)
# define RTStrAssertMsgReturn(expr, msg, rc)    AssertMsgReturn(expr, msg, rc)
#else
# define RTStrAssertMsgFailed(msg)              do { } while (0)
# define RTStrAssertMsgReturn(expr, msg, rc)    do { if (!(expr)) return rc; } while (0)
#endif

DECLHIDDEN(size_t) rtStrFormatBadPointer(size_t cch, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, int cchWidth,
                                         unsigned fFlags, void const *pvStr, char szTmp[64], const char *pszTag, int cchTag);
DECLHIDDEN(size_t) rtstrFormatRt(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, const char **ppszFormat, va_list *pArgs,
                                 int cchWidth, int cchPrecision, unsigned fFlags, char chArgSize);
DECLHIDDEN(size_t) rtstrFormatType(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, const char **ppszFormat, va_list *pArgs,
                                   int cchWidth, int cchPrecision, unsigned fFlags, char chArgSize);

/**
 * Format kernel address into @a pszBuf.
 *
 * @returns Number of bytes returned.
 * @param   pszBuf          The return buffer.
 * @param   cbBuf           The buffer size.
 * @param   uPtr            The ring-0 pointer value.
 * @param   cchWidth        The specified width, -1 if not given.
 * @param   cchPrecision    The specified precision.
 * @param   fFlags          Format flags, RTSTR_F_XXX.
 */
DECLHIDDEN(size_t) rtStrFormatKernelAddress(char *pszBuf, size_t cbBuf, RTR0INTPTR uPtr, signed int cchWidth,
                                            signed int cchPrecision, unsigned int fFlags);

#ifdef RT_WITH_ICONV_CACHE
DECLHIDDEN(void) rtStrIconvCacheInit(struct RTTHREADINT *pThread);
DECLHIDDEN(void) rtStrIconvCacheDestroy(struct RTTHREADINT *pThread);
#endif

/**
 * Indexes into RTTHREADINT::ahIconvs
 */
typedef enum RTSTRICONV
{
    /** UTF-8 to the locale codeset (LC_CTYPE). */
    RTSTRICONV_UTF8_TO_LOCALE = 0,
    /** The locale codeset (LC_CTYPE) to UTF-8. */
    RTSTRICONV_LOCALE_TO_UTF8,
    /** UTF-8 to the filesystem codeset - if different from the locale codeset. */
    RTSTRICONV_UTF8_TO_FS,
    /** The filesystem codeset to UTF-8. */
    RTSTRICONV_FS_TO_UTF8,
    /** The end of the valid indexes. */
    RTSTRICONV_END
} RTSTRICONV;

DECLHIDDEN(int) rtStrConvert(const char *pchInput, size_t cchInput, const char *pszInputCS,
                             char **ppszOutput, size_t cbOutput, const char *pszOutputCS,
                             unsigned cFactor, RTSTRICONV enmCacheIdx);
DECLHIDDEN(void) rtStrLocalCacheInit(void **ppvTmpCache);
DECLHIDDEN(int)  rtStrLocalCacheConvert(const char *pchInput, size_t cchInput, const char *pszInputCS,
                                        char **ppszOutput, size_t cbOutput, const char *pszOutputCS,
                                        void **ppvTmpCache);
DECLHIDDEN(void) rtStrLocalCacheDelete(void **ppvTmpCache);
DECLHIDDEN(const char *) rtStrGetLocaleCodeset(void);
DECLHIDDEN(bool)         rtStrIsLocaleCodesetUtf8(void);
DECLHIDDEN(bool)         rtStrIsCodesetUtf8(const char *pszCodeset);
DECLHIDDEN(int) rtUtf8Length(const char *psz, size_t cch, size_t *pcuc, size_t *pcchActual);

DECLHIDDEN(int) rtStrToIpAddr6Str(const char *psz, char *pszAddrOut, size_t addrOutSize, char *pszPortOut, size_t portOutSize, bool followRfc);

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_string_h */

