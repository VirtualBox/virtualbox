/* $Id: RTTimeZoneGetCurrent-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTTimeZoneGetCurrent, POSIX.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <iprt/time.h>
#include "internal/iprt.h"

#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/errcore.h>
#include <iprt/types.h>
#include <iprt/symlink.h>
#include <iprt/stream.h>

#if defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS)
# include <tzfile.h>
#else
# define TZDIR                   "/usr/share/zoneinfo"
# define TZ_MAGIC                "TZif"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define PATH_LOCALTIME           "/etc/localtime"
#if defined(RT_OS_FREEBSD)
# define PATH_TIMEZONE           "/var/db/zoneinfo"
#else
# define PATH_TIMEZONE           "/etc/timezone"
#endif
#define PATH_SYSCONFIG_CLOCK     "/etc/sysconfig/clock"


/**
 * Checks if a time zone database file is valid by verifying it begins with
 * TZ_MAGIC.
 *
 * @returns IPRT status code.
 * @param   pszTimezone         The time zone database file relative to
 *                              <tzfile.h>:TZDIR (normally /usr/share/zoneinfo),
 *                              e.g. Europe/London, or Etc/UTC, or UTC, or etc.
 *
 * @note    File format is documented in RFC-8536.
 */
static int rtIsValidTimeZoneFile(const char *pszTimeZone)
{
    if (pszTimeZone == NULL || *pszTimeZone == '\0' || *pszTimeZone == '/')
        return VERR_INVALID_PARAMETER;

    int rc = RTStrValidateEncoding(pszTimeZone);
    if (RT_SUCCESS(rc))
    {
        /* construct full pathname of the time zone file */
        char szTZPath[RTPATH_MAX];
        rc = RTPathJoin(szTZPath, sizeof(szTZPath), TZDIR, pszTimeZone);
        if (RT_SUCCESS(rc))
        {
            /* open the time zone file and check that it begins with the correct magic number */
            RTFILE hFile = NIL_RTFILE;
            rc = RTFileOpen(&hFile, szTZPath, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
            if (RT_SUCCESS(rc))
            {
                char achTZBuf[sizeof(TZ_MAGIC)];
                rc = RTFileRead(hFile, achTZBuf, sizeof(achTZBuf), NULL);
                RTFileClose(hFile);
                if (RT_SUCCESS(rc))
                {
                    if (memcmp(achTZBuf, RT_STR_TUPLE(TZ_MAGIC)) == 0)
                        rc = VINF_SUCCESS;
                    else
                        rc = VERR_INVALID_MAGIC;
                }
            }
        }
    }

    return rc;
}


/**
 * Return the system time zone.
 *
 * @returns IPRT status code.
 * @param   pszName         The buffer to return the time zone in.
 * @param   cbName          The size of the pszName buffer.
 */
RTDECL(int) RTTimeZoneGetCurrent(char *pszName, size_t cbName)
{
    int rc = RTEnvGetEx(RTENV_DEFAULT, "TZ", pszName, cbName, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * $TZ can have two different formats and one of them doesn't specify
         * a time zone database file under <tzfile.h>:TZDIR but since all
         * current callers of this routine expect a time zone filename we do
         * the validation check here so that if it is invalid then we fall back
         * to the other mechanisms to return the system's current time zone.
         */
        if (*pszName == ':') /* POSIX allows $TZ to begin with a colon (:) so we allow for that here */
            memmove(pszName, pszName + 1, strlen(pszName));
        /** @todo this isn't perfect for absolute paths... Should probably try treat
         *        it like /etc/localtime. */
        rc = rtIsValidTimeZoneFile(pszName);
        if (RT_SUCCESS(rc))
            return rc;
    }
    else if (rc != VERR_ENV_VAR_NOT_FOUND)
        return rc;

    /*
     * /etc/localtime is a symbolic link to the system time zone on many OSes
     * including Solaris, macOS, Ubuntu, RH/OEL 6 and later, Arch Linux, NetBSD,
     * and etc.  We extract the time zone pathname relative to TZDIR defined in
     * <tzfile.h> which is normally /usr/share/zoneinfo.
     *
     * N.B. Some OSes have /etc/localtime as a regular file instead of a
     * symlink and while we could trawl through all the files under TZDIR
     * looking for a match we instead fallback to other popular mechanisms of
     * specifying the system-wide time zone for the sake of simplicity.
     */
    char szBuf[RTPATH_MAX];
    const char *pszPath = PATH_LOCALTIME;
    if (RTSymlinkExists(pszPath))
    {
        /* the contents of the symink may contain '..' or other links */
        char szLinkPathReal[RTPATH_MAX];
        rc = RTPathReal(pszPath, szLinkPathReal, sizeof(szLinkPathReal));
        if (RT_SUCCESS(rc))
        {
            rc = RTPathReal(TZDIR, szBuf, sizeof(szBuf));
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                Assert(RTPathStartsWith(szLinkPathReal, szBuf));
                if (RTPathStartsWith(szLinkPathReal, szBuf))
                {
                    /* <tzfile.h>:TZDIR doesn't include a trailing slash */
                    const char *pszTimeZone = &szLinkPathReal[strlen(szBuf) + 1];
                    rc = rtIsValidTimeZoneFile(pszTimeZone);
                    if (RT_SUCCESS(rc))
                        return RTStrCopy(pszName, cbName, pszTimeZone);
                }
            }
        }
    }

    /*
     * /etc/timezone is a regular file consisting of a single line containing
     * the time zone (e.g. Europe/London or Etc/UTC or etc.) and is used by a
     * variety of Linux distros such as Ubuntu, Gentoo, Debian, and etc.
     * The equivalent on FreeBSD is /var/db/zoneinfo.
     */
    pszPath = PATH_TIMEZONE;
    if (RTFileExists(pszPath))
    {
        RTFILE hFile = NIL_RTFILE;
        rc = RTFileOpen(&hFile, PATH_TIMEZONE, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
        if (RT_SUCCESS(rc))
        {
            size_t cbRead = 0;
            rc = RTFileRead(hFile, szBuf, sizeof(szBuf), &cbRead);
            RTFileClose(hFile);
            if (RT_SUCCESS(rc) && cbRead > 0)
            {
                /* Get the first line and strip it. */
                szBuf[RT_MIN(sizeof(szBuf) - 1, cbRead)] = '\0';
                size_t const offNewLine = RTStrOffCharOrTerm(szBuf, '\n');
                szBuf[offNewLine] = '\0';
                const char *pszTimeZone = RTStrStrip(szBuf);

                rc = rtIsValidTimeZoneFile(pszTimeZone);
                if (RT_SUCCESS(rc))
                    return RTStrCopy(pszName, cbName, pszTimeZone);
            }
        }
    }

    /*
     * Older versions of RedHat / OEL don't have /etc/localtime as a symlink or
     * /etc/timezone but instead have /etc/sysconfig/clock which contains a line
     * of the syntax ZONE=Europe/London or ZONE="Europe/London" amongst other entries.
     */
    pszPath = PATH_SYSCONFIG_CLOCK;
    if (RTFileExists(pszPath))
    {
        PRTSTREAM pStrm;
        rc = RTStrmOpen(pszPath, "r", &pStrm);
        if (RT_SUCCESS(rc))
        {
            while (RT_SUCCESS(rc = RTStrmGetLine(pStrm, szBuf, sizeof(szBuf))))
            {
                static char const s_szVarEq[] = "ZONE=";
                char             *pszStart    = RTStrStrip(szBuf);
                if (memcmp(pszStart, RT_STR_TUPLE(s_szVarEq)) == 0)
                {
                    char *pszTimeZone = &pszStart[sizeof(s_szVarEq) - 1];

                    /* Drop any quoting before using the value, assuming it is plain stuff: */
                    if (*pszTimeZone == '\"' || *pszTimeZone == '\'')
                    {
                        pszTimeZone++;
                        size_t const cchTimeZone = strlen(pszTimeZone);
                        if (cchTimeZone && (pszTimeZone[cchTimeZone - 1] == '"' || pszTimeZone[cchTimeZone - 1] == '\''))
                            pszTimeZone[cchTimeZone - 1] = '\0';
                    }

                    rc = rtIsValidTimeZoneFile(pszTimeZone);
                    if (RT_SUCCESS(rc))
                    {
                        RTStrmClose(pStrm);
                        return RTStrCopy(pszName, cbName, pszTimeZone);
                    }
                }
            }
            RTStrmClose(pStrm);
        }
    }

    return rc;
}

