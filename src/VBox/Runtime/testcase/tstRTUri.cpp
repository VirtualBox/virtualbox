/* $Id: tstRTUri.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - URI parsing and creation.
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
#include <iprt/uri.h>

#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/test.h>

#if 0 && defined(RT_OS_WINDOWS) /* Enable for windows API reference results. */
# define TSTRTURI_WITH_WINDOWS_REFERENCE_RESULTS
# include <iprt/win/shlwapi.h>
# include <iprt/stream.h>
#endif


/*********************************************************************************************************************************
*   Test data                                                                                                                    *
*********************************************************************************************************************************/

static struct
{
    const char *pszUri;
    const char *pszScheme;
    const char *pszAuthority;
    const char *pszPath;
    const char *pszQuery;
    const char *pszFragment;

    const char *pszUsername;
    const char *pszPassword;
    const char *pszHost;
    uint32_t    uPort;

    const char *pszCreated;
} g_aTests[] =
{
    {   /* #0 */
        "foo://tt:yt@example.com:8042/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there?name=%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60ferret#nose%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "tt:yt@example.com:8042",
        /*.pszPath      =*/ "/over/ <>#%\"{}|^[]`/there",
        /*.pszQuery     =*/ "name= <>#%\"{}|^[]`ferret",
        /*.pszFragment  =*/ "nose <>#%\"{}|^[]`",
        /*.pszUsername  =*/ "tt",
        /*.pszPassword  =*/ "yt",
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ 8042,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #1 */
        "foo://tt:yt@example.com:8042/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there?name=%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60ferret",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "tt:yt@example.com:8042",
        /*.pszPath      =*/ "/over/ <>#%\"{}|^[]`/there",
        /*.pszQuery     =*/ "name= <>#%\"{}|^[]`ferret",
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ "tt",
        /*.pszPassword  =*/ "yt",
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ 8042,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #2 */
        "foo://tt:yt@example.com:8042/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "tt:yt@example.com:8042",
        /*.pszPath      =*/ "/over/ <>#%\"{}|^[]`/there",
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ "tt",
        /*.pszPassword  =*/ "yt",
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ 8042,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #3 */
        "foo:tt@example.com",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ NULL,
        /*.pszPath      =*/ "tt@example.com",
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #4 */
        "foo:/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there?name=%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60ferret#nose%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ NULL,
        /*.pszPath      =*/ "/over/ <>#%\"{}|^[]`/there",
        /*.pszQuery     =*/ "name= <>#%\"{}|^[]`ferret",
        /*.pszFragment  =*/ "nose <>#%\"{}|^[]`",
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #5 */
        "foo:/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there#nose%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ NULL,
        /*.pszPath      =*/ "/over/ <>#%\"{}|^[]`/there",
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ "nose <>#%\"{}|^[]`",
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #6 */
        "urn:example:animal:ferret:nose",
        /*.pszScheme    =*/ "urn",
        /*.pszAuthority =*/ NULL,
        /*.pszPath      =*/ "example:animal:ferret:nose",
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #7 */
        "foo:?name=%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60ferret#nose%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ NULL,
        /*.pszPath      =*/ NULL,
        /*.pszQuery     =*/ "name= <>#%\"{}|^[]`ferret",
        /*.pszFragment  =*/ "nose <>#%\"{}|^[]`",
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #8 */
        "foo:#nose%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ NULL,
        /*.pszPath      =*/ NULL,
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ "nose <>#%\"{}|^[]`",
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #9 */
        "foo://tt:yt@example.com:8042/?name=%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60ferret#nose%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "tt:yt@example.com:8042",
        /*.pszPath      =*/ "/",
        /*.pszQuery     =*/ "name= <>#%\"{}|^[]`ferret",
        /*.pszFragment  =*/ "nose <>#%\"{}|^[]`",
        /*.pszUsername  =*/ "tt",
        /*.pszPassword  =*/ "yt",
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ 8042,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #10 */
        "foo://tt:yt@example.com:8042/",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "tt:yt@example.com:8042",
        /*.pszPath      =*/ "/",
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ "tt",
        /*.pszPassword  =*/ "yt",
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ 8042,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #11 */
        "foo://tt:yt@example.com:8042?name=%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60ferret#nose%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "tt:yt@example.com:8042",
        /*.pszPath      =*/ NULL,
        /*.pszQuery     =*/ "name= <>#%\"{}|^[]`ferret",
        /*.pszFragment  =*/ "nose <>#%\"{}|^[]`",
        /*.pszUsername  =*/ "tt",
        /*.pszPassword  =*/ "yt",
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ 8042,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #12 */
        "foo://tt:yt@example.com:8042#nose%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "tt:yt@example.com:8042",
        /*.pszPath      =*/ NULL,
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ "nose <>#%\"{}|^[]`",
        /*.pszUsername  =*/ "tt",
        /*.pszPassword  =*/ "yt",
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ 8042,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #13 */
        "foo://tt:yt@example.com:8042",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "tt:yt@example.com:8042",
        /*.pszPath      =*/ NULL,
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ "tt",
        /*.pszPassword  =*/ "yt",
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ 8042,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #14 */
        "file:///dir/dir/file",
        /*.pszScheme    =*/ "file",
        /*.pszAuthority =*/ "",
        /*.pszPath      =*/ "/dir/dir/file",
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #15 */
        "foo:///",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "",
        /*.pszPath      =*/ "/",
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #16 */
        "foo://",
        /*.pszScheme    =*/ "foo",
        /*.pszAuthority =*/ "",
        /*.pszPath      =*/ NULL,
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ NULL,
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ NULL /* same as pszUri*/,
    },
    {   /* #17 - UTF-8 escape sequences. */
        "http://example.com/%ce%b3%ce%bb%cf%83%ce%b1%20%e0%a4%95\xe0\xa4\x95",
        /*.pszScheme    =*/ "http",
        /*.pszAuthority =*/ "example.com",
        /*.pszPath      =*/ "/\xce\xb3\xce\xbb\xcf\x83\xce\xb1 \xe0\xa4\x95\xe0\xa4\x95",
        /*.pszQuery     =*/ NULL,
        /*.pszFragment  =*/ NULL,
        /*.pszUsername  =*/ NULL,
        /*.pszPassword  =*/ NULL,
        /*.pszHost      =*/ "example.com",
        /*.uPort        =*/ UINT32_MAX,
        /*.pszCreated   =*/ "http://example.com/\xce\xb3\xce\xbb\xcf\x83\xce\xb1%20\xe0\xa4\x95\xe0\xa4\x95",
    },
};


static struct URIFILETEST
{
    const char     *pszPath;
    uint32_t        fPathPathStyle;
    const char     *pszUri;
    uint32_t        fUriPathStyle;
    const char     *pszCreatedPath;
    const char     *pszCreatedUri;
} g_aCreateFileURIs[] =
{
    {   /* #0: */
        /* .pszPath          =*/ "C:\\over\\ <>#%\"{}|^[]`\\there",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:///C:%5Cover/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60%5Cthere",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "C:\\over\\ <>#%\"{}|^[]`\\there",
        /* .pszCreatedUri    =*/ "file:///C:/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there",
        /* PathCreateFromUrl =   "C:\\over\\ <>#%\"{}|^[]`\\there" - same */
        /* UrlCreateFromPath =   "file:///C:/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there" - same */
    },
    {   /* #1: */
        /* .pszPath          =*/ "/over/ <>#%\"{}|^[]`/there",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszUri           =*/ "file:///over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszCreatedPath   =*/ "/over/ <>#%\"{}|^[]`/there",
        /* .pszCreatedUri    =*/ "file:///over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there",
        /* PathCreateFromUrl =   "\\over\\ <>#%\"{}|^[]`\\there" - differs */
        /* UrlCreateFromPath =   "file:///over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there" - same */
    },
    {   /* #2: */
        /* .pszPath          =*/ NULL,
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszUri           =*/ "file://",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszCreatedPath   =*/ NULL,
        /* .pszCreatedUri    =*/ NULL,
        /* PathCreateFromUrl =   "" - differs */
        /* UrlCreateFromPath => 0x80070057 (E_INVALIDARG) */
    },
    {   /* #3: */
        /* .pszPath          =*/ NULL,
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file://",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ NULL,
        /* .pszCreatedUri    =*/ NULL,
        /* PathCreateFromUrl =   "" - differs */
        /* UrlCreateFromPath => 0x80070057 (E_INVALIDARG) */
    },
    {   /* #4: */
        /* .pszPath          =*/ "/",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszUri           =*/ "file:///",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszCreatedPath   =*/ "/",
        /* .pszCreatedUri    =*/ "file:///",
        /* PathCreateFromUrl =   "" - differs */
        /* UrlCreateFromPath =   "file:///" - same */
    },
    {   /* #5: */
        /* .pszPath          =*/ "\\",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:///",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\",
        /* .pszCreatedUri    =*/ "file:///",
        /* PathCreateFromUrl =   "" - differs */
        /* UrlCreateFromPath =   "file:///" - same */
    },
    {   /* #6: */
        /* .pszPath          =*/ "/foo/bar",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszUri           =*/ "file:///foo/bar",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszCreatedPath   =*/ "/foo/bar",
        /* .pszCreatedUri    =*/ "file:///foo/bar",
        /* PathCreateFromUrl =   "\\foo\\bar" - differs */
        /* UrlCreateFromPath =   "file:///foo/bar" - same */
    },
    {   /* #7: */
        /* .pszPath          =*/ "\\foo\\bar",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:///foo%5Cbar",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\foo\\bar",
        /* .pszCreatedUri    =*/ "file:///foo/bar",
        /* PathCreateFromUrl =   "\\foo\\bar" - same */
        /* UrlCreateFromPath =   "file:///foo/bar" - same */
    },
    {   /* #8: */
        /* .pszPath          =*/ "C:/over/ <>#%\"{}|^[]`/there",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszUri           =*/ "file:///C:/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszCreatedPath   =*/ "C:/over/ <>#%\"{}|^[]`/there",
        /* .pszCreatedUri    =*/ "file:///C:/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there",
        /* PathCreateFromUrl =   "C:\\over\\ <>#%\"{}|^[]`\\there" - differs */
        /* UrlCreateFromPath =   "file:///C:/over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there" - same */
    },
    {   /* #9: */
        /* .pszPath          =*/ "\\over\\ <>#%\"{}|^[]`\\there",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:///over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\over\\ <>#%\"{}|^[]`\\there",
        /* .pszCreatedUri    =*/ "file:///over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there",
        /* PathCreateFromUrl =   "\\over\\ <>#%\"{}|^[]`\\there" - same */
        /* UrlCreateFromPath =   "file:///over/%20%3C%3E%23%25%22%7B%7D%7C%5E%5B%5D%60/there" - same */
    },
    {   /* #10: */
        /* .pszPath          =*/ "/usr/bin/grep",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszUri           =*/ "file:///usr/bin/grep",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszCreatedPath   =*/ "/usr/bin/grep",
        /* .pszCreatedUri    =*/ "file:///usr/bin/grep",
        /* PathCreateFromUrl =   "\\usr\\bin\\grep" - differs */
        /* UrlCreateFromPath =   "file:///usr/bin/grep" - same */
    },
    {   /* #11: */
        /* .pszPath          =*/ "\\usr\\bin\\grep",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:///usr/bin/grep",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\usr\\bin\\grep",
        /* .pszCreatedUri    =*/ "file:///usr/bin/grep",
        /* PathCreateFromUrl =   "\\usr\\bin\\grep" - same */
        /* UrlCreateFromPath =   "file:///usr/bin/grep" - same */
    },
    {   /* #12: */
        /* .pszPath          =*/ "/somerootsubdir/isos/files.lst",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszUri           =*/ "file:///somerootsubdir/isos/files.lst",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszCreatedPath   =*/ "/somerootsubdir/isos/files.lst",
        /* .pszCreatedUri    =*/ "file:///somerootsubdir/isos/files.lst",
        /* PathCreateFromUrl =   "\\somerootsubdir\\isos\\files.lst" - differs */
        /* UrlCreateFromPath =   "file:///somerootsubdir/isos/files.lst" - same */
    },
    {   /* #13: */
        /* .pszPath          =*/ "\\not-a-cifsserver\\isos\\files.lst",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:///not-a-cifsserver/isos/files.lst",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\not-a-cifsserver\\isos\\files.lst",
        /* .pszCreatedUri    =*/ "file:///not-a-cifsserver/isos/files.lst",
        /* PathCreateFromUrl =   "\\not-a-cifsserver\\isos\\files.lst" - same */
        /* UrlCreateFromPath =   "file:///not-a-cifsserver/isos/files.lst" - same */
    },
    {   /* #14: */
        /* .pszPath          =*/ "/rootsubdir/isos/files.lst",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszUri           =*/ "file:///rootsubdir/isos/files.lst",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_UNIX,
        /* .pszCreatedPath   =*/ "/rootsubdir/isos/files.lst",
        /* .pszCreatedUri    =*/ "file:///rootsubdir/isos/files.lst",
        /* PathCreateFromUrl =   "\\rootsubdir\\isos\\files.lst" - differs */
        /* UrlCreateFromPath =   "file:///rootsubdir/isos/files.lst" - same */
    },
    {   /* #15: */
        /* .pszPath          =*/ "\\not-a-cifsserver-either\\isos\\files.lst",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:///not-a-cifsserver-either/isos/files.lst",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\not-a-cifsserver-either\\isos\\files.lst",
        /* .pszCreatedUri    =*/ "file:///not-a-cifsserver-either/isos/files.lst",
        /* PathCreateFromUrl =   "\\not-a-cifsserver-either\\isos\\files.lst" - same */
        /* UrlCreateFromPath =   "file:///not-a-cifsserver-either/isos/files.lst" - same */
    },
    {   /* #16: */
        /* .pszPath          =*/ "\\\\cifsserver\\isos\\files.lst",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:////cifsserver/isos/files.lst",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\\\cifsserver\\isos\\files.lst",
        /* .pszCreatedUri    =*/ "file://cifsserver/isos/files.lst",
        /* PathCreateFromUrl =   "\\\\cifsserver\\isos\\files.lst" - same */
        /* UrlCreateFromPath =   "file://cifsserver/isos/files.lst" - same */
    },
    {   /* #17: */
        /* .pszPath          =*/ "c:boot.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file://localhost/c:boot.ini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "c:boot.ini",
        /* .pszCreatedUri    =*/ "file:///c:boot.ini",
        /* PathCreateFromUrl =   "c:boot.ini" - same */
        /* UrlCreateFromPath =   "file:///c:boot.ini" - same */
    },
    {   /* #18: */
        /* .pszPath          =*/ "c:boot.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:///c|boot.ini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "c:boot.ini",
        /* .pszCreatedUri    =*/ "file:///c:boot.ini",
        /* PathCreateFromUrl =   "c:boot.ini" - same */
        /* UrlCreateFromPath =   "file:///c:boot.ini" - same */
    },
    {   /* #19: */
        /* .pszPath          =*/ "c:\\legacy-no-slash.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:c:\\legacy-no-slash%2Eini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "c:\\legacy-no-slash.ini",
        /* .pszCreatedUri    =*/ "file:///c:/legacy-no-slash.ini",
        /* PathCreateFromUrl =   "c:\\legacy-no-slash.ini" - same */
        /* UrlCreateFromPath =   "file:///c:/legacy-no-slash.ini" - same */
    },
    {   /* #20: */
        /* .pszPath          =*/ "c:\\legacy-no-slash.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:c|\\legacy-no-slash%2Eini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "c:\\legacy-no-slash.ini",
        /* .pszCreatedUri    =*/ "file:///c:/legacy-no-slash.ini",
        /* PathCreateFromUrl =   "c:\\legacy-no-slash.ini" - same */
        /* UrlCreateFromPath =   "file:///c:/legacy-no-slash.ini" - same */
    },
    {   /* #21: */
        /* .pszPath          =*/ "c:\\legacy-single-slash.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:/c:\\legacy-single-slash%2Eini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "c:\\legacy-single-slash.ini",
        /* .pszCreatedUri    =*/ "file:///c:/legacy-single-slash.ini",
        /* PathCreateFromUrl =   "c:\\legacy-single-slash.ini" - same */
        /* UrlCreateFromPath =   "file:///c:/legacy-single-slash.ini" - same */
    },
    {   /* #22: */
        /* .pszPath          =*/ "c:\\legacy-single-slash.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:/c:\\legacy-single-slash%2Eini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "c:\\legacy-single-slash.ini",
        /* .pszCreatedUri    =*/ "file:///c:/legacy-single-slash.ini",
        /* PathCreateFromUrl =   "c:\\legacy-single-slash.ini" - same */
        /* UrlCreateFromPath =   "file:///c:/legacy-single-slash.ini" - same */
    },
    {   /* #23: */
        /* .pszPath          =*/ "\\legacy-single-slash.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:/legacy-single-slash%2Eini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\legacy-single-slash.ini",
        /* .pszCreatedUri    =*/ "file:///legacy-single-slash.ini",
        /* PathCreateFromUrl =   "\\legacy-single-slash.ini" - same */
        /* UrlCreateFromPath =   "file:///legacy-single-slash.ini" - same */
    },
    {   /* #24: */
        /* .pszPath          =*/ "C:\\legacy-double-slash%2E.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file://C:\\legacy-double-slash%2E.ini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "C:\\legacy-double-slash%2E.ini",
        /* .pszCreatedUri    =*/ "file:///C:/legacy-double-slash%252E.ini",
        /* PathCreateFromUrl =   "C:\\legacy-double-slash%2E.ini" - same */
        /* UrlCreateFromPath =   "file:///C:/legacy-double-slash.ini" - same */
    },
    {   /* #25: */
        /* .pszPath          =*/ "C:\\legacy-double-slash%2E.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file://C|/legacy-double-slash%2E.ini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "C:\\legacy-double-slash%2E.ini",
        /* .pszCreatedUri    =*/ "file:///C:/legacy-double-slash%252E.ini",
        /* PathCreateFromUrl =   "C:\\legacy-double-slash%2E.ini" - same */
        /* UrlCreateFromPath =   "file:///C:/legacy-double-slash%252E.ini" - same */
    },
    {   /* #26: */
        /* .pszPath          =*/ "C:\\legacy-4-slashes%2E.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:////C|/legacy-4-slashes%2E.ini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "C:\\legacy-4-slashes%2E.ini",
        /* .pszCreatedUri    =*/ "file:///C:/legacy-4-slashes%252E.ini",
        /* PathCreateFromUrl =   "C:\\legacy-4-slashes%2E.ini" - same */
        /* UrlCreateFromPath =   "file:///C:/legacy-4-slashes%252E.ini" - same */
    },
    {   /* #27: */
        /* .pszPath          =*/ "C:\\legacy-4-slashes%2E.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:////C:/legacy-4-slashes%2E.ini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "C:\\legacy-4-slashes%2E.ini",
        /* .pszCreatedUri    =*/ "file:///C:/legacy-4-slashes%252E.ini",
        /* PathCreateFromUrl =   "C:\\legacy-4-slashes%2E.ini" - same */
        /* UrlCreateFromPath =   "file:///C:/legacy-4-slashes%252E.ini" - same */
    },
    {   /* #28: */
        /* .pszPath          =*/ "\\\\cifsserver\\share\\legacy-4-slashes%2E.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file:////cifsserver/share/legacy-4-slashes%2E.ini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\\\cifsserver\\share\\legacy-4-slashes%2E.ini",
        /* .pszCreatedUri    =*/ "file://cifsserver/share/legacy-4-slashes%252E.ini",
        /* PathCreateFromUrl =   "\\\\cifsserver\\share\\legacy-4-slashes%2E.ini" - same */
        /* UrlCreateFromPath =   "file://cifsserver/share/legacy-4-slashes%252E.ini" - same */
    },
    {   /* #29: */
        /* .pszPath          =*/ "\\\\cifsserver\\share\\legacy-5-slashes.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file://///cifsserver/share/legacy-5-slashes%2Eini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\\\cifsserver\\share\\legacy-5-slashes.ini",
        /* .pszCreatedUri    =*/ "file://cifsserver/share/legacy-5-slashes.ini",
        /* PathCreateFromUrl =   "\\\\cifsserver\\share\\legacy-5-slashes.ini" - same */
        /* UrlCreateFromPath =   "file://cifsserver/share/legacy-5-slashes.ini" - same */
    },
    {   /* #30: */
        /* .pszPath          =*/ "\\\\C|\\share\\legacy-5-slashes.ini",
        /* .fPathPathStyle   =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszUri           =*/ "file://///C|/share/legacy-5-slashes%2Eini",
        /* .fUriPathStyle    =*/ RTPATH_STR_F_STYLE_DOS,
        /* .pszCreatedPath   =*/ "\\\\C|\\share\\legacy-5-slashes.ini",
        /* .pszCreatedUri    =*/ "file://C%7C/share/legacy-5-slashes.ini",
        /* PathCreateFromUrl =   "\\\\C|\\share\\legacy-5-slashes.ini" - same */
        /* UrlCreateFromPath =   "file:///C%7C/share/legacy-5-slashes.ini" - differs */
    },
};

#ifdef TSTRTURI_WITH_WINDOWS_REFERENCE_RESULTS

static void tstPrintCString(const char *pszString)
{
    if (pszString)
    {
        char ch;
        RTPrintf("\"");
        while ((ch = *pszString++) != '\0')
        {
            if (ch >= 0x20 && ch < 0x7f)
                switch (ch)
                {
                    default:
                        RTPrintf("%c", ch);
                        break;
                    case '\\':
                    case '"':
                        RTPrintf("\\%c", ch);
                        break;
                }
            else
                RTPrintf("\\x%02X", ch); /* good enough */
        }
        RTPrintf("\"");
    }
    else
        RTPrintf("NULL");
}

static const char *tstNamePathStyle(uint32_t fPathStyle)
{
    switch (fPathStyle)
    {
        case RTPATH_STR_F_STYLE_DOS:   return "RTPATH_STR_F_STYLE_DOS";
        case RTPATH_STR_F_STYLE_UNIX:  return "RTPATH_STR_F_STYLE_UNIX";
        case RTPATH_STR_F_STYLE_HOST:  return "RTPATH_STR_F_STYLE_HOST";
        default: AssertFailedReturn("Invalid");
    }
}

static void tstWindowsReferenceResults(void)
{
    /*
     * Feed the g_aCreateFileURIs values as input to the Windows
     * PathCreateFromUrl and URlCreateFromPath APIs and print the results.
     *
     * We reproduce the entire source file content of g_aCreateFileURIs here.
     */
    for (size_t i = 0; i < RT_ELEMENTS(g_aCreateFileURIs); ++i)
    {
        RTPrintf("    {   /* #%u: */\n", i);
        RTPrintf("        /* .pszPath          =*/ ");
        tstPrintCString(g_aCreateFileURIs[i].pszPath);
        RTPrintf(",\n");
        RTPrintf("        /* .fPathPathStyle   =*/ %s,\n", tstNamePathStyle(g_aCreateFileURIs[i].fPathPathStyle));
        RTPrintf("        /* .pszUri           =*/ ");
        tstPrintCString(g_aCreateFileURIs[i].pszUri);
        RTPrintf(",\n");
        RTPrintf("        /* .fUriPathStyle    =*/ %s,\n", tstNamePathStyle(g_aCreateFileURIs[i].fUriPathStyle));
        RTPrintf("        /* .pszCreatedPath   =*/ ");
        tstPrintCString(g_aCreateFileURIs[i].pszCreatedPath);
        RTPrintf(",\n");
        RTPrintf("        /* .pszCreatedUri    =*/ ");
        tstPrintCString(g_aCreateFileURIs[i].pszCreatedUri);
        RTPrintf(",\n");

        /*
         * PathCreateFromUrl
         */
        PRTUTF16 pwszInput = NULL;
        if (g_aCreateFileURIs[i].pszUri)
            RTTESTI_CHECK_RC_OK_RETV(RTStrToUtf16(g_aCreateFileURIs[i].pszUri, &pwszInput));
        WCHAR wszResult[_1K];
        DWORD cwcResult = RT_ELEMENTS(wszResult);
        RT_ZERO(wszResult);
        HRESULT hrc = PathCreateFromUrlW(pwszInput, wszResult, &cwcResult, 0 /*dwFlags*/);
        RTUtf16Free(pwszInput);

        if (SUCCEEDED(hrc))
        {
            char *pszResult;
            RTTESTI_CHECK_RC_OK_RETV(RTUtf16ToUtf8(wszResult, &pszResult));
            RTPrintf("        /* PathCreateFromUrl =   ");
            tstPrintCString(pszResult);
            if (   g_aCreateFileURIs[i].pszCreatedPath
                && strcmp(pszResult, g_aCreateFileURIs[i].pszCreatedPath) == 0)
                RTPrintf(" - same */\n");
            else
                RTPrintf(" - differs */\n");
            RTStrFree(pszResult);
        }
        else
            RTPrintf("        /* PathCreateFromUrl => %#x (%Rhrc) */\n", hrc, hrc);

        /*
         * UrlCreateFromPath + UrlEscape
         */
        pwszInput = NULL;
        if (g_aCreateFileURIs[i].pszPath)
            RTTESTI_CHECK_RC_OK_RETV(RTStrToUtf16(g_aCreateFileURIs[i].pszPath, &pwszInput));
        RT_ZERO(wszResult);
        cwcResult = RT_ELEMENTS(wszResult);
        hrc = UrlCreateFromPathW(pwszInput, wszResult, &cwcResult, 0 /*dwFlags*/);
        RTUtf16Free(pwszInput);

        if (SUCCEEDED(hrc))
        {
            WCHAR wszResult2[_1K];
            DWORD cwcResult2 = RT_ELEMENTS(wszResult2);
            hrc = UrlEscapeW(wszResult, wszResult2, &cwcResult2, URL_DONT_ESCAPE_EXTRA_INFO | URL_ESCAPE_AS_UTF8 );
            if (SUCCEEDED(hrc))
            {
                char *pszResult;
                RTTESTI_CHECK_RC_OK_RETV(RTUtf16ToUtf8(wszResult2, &pszResult));
                RTPrintf("        /* UrlCreateFromPath =   ");
                tstPrintCString(pszResult);
                if (   g_aCreateFileURIs[i].pszCreatedUri
                    && strcmp(pszResult, g_aCreateFileURIs[i].pszCreatedUri) == 0)
                    RTPrintf(" - same */\n");
                else
                    RTPrintf(" - differs */\n");
                RTStrFree(pszResult);
            }
            else
                RTPrintf("        /* UrlEscapeW        => %#x (%Rhrc) */\n", hrc, hrc);
        }
        else
            RTPrintf("        /* UrlCreateFromPath => %#x (%Rhrc) */\n", hrc, hrc);
        RTPrintf("    },\n");
    }
}

#endif /* TSTRTURI_WITH_WINDOWS_REFERENCE_RESULTS */


static void tstRTUriFilePathEx(void)
{
    RTTestISub("RTUriFilePathEx");
    for (size_t i = 0; i < RT_ELEMENTS(g_aCreateFileURIs); ++i)
    {
        uint32_t const     fPathStyle = g_aCreateFileURIs[i].fUriPathStyle;
        const char * const pszUri     = g_aCreateFileURIs[i].pszUri;
        char *pszPath = (char *)&pszPath;
        int rc = RTUriFilePathEx(pszUri, fPathStyle, &pszPath, 0, NULL);
        if (RT_SUCCESS(rc))
        {
            if (g_aCreateFileURIs[i].pszCreatedPath)
            {
                if (strcmp(pszPath, g_aCreateFileURIs[i].pszCreatedPath) == 0)
                {
                    /** @todo check out the other variations of the API. */
                }
                else
                    RTTestIFailed("#%u: '%s'/%#x => '%s', expected '%s'",
                                  i, pszUri, fPathStyle, pszPath, g_aCreateFileURIs[i].pszCreatedPath);
            }
            else
                RTTestIFailed("#%u: bad testcase; pszCreatedPath is NULL\n", i);
            RTStrFree(pszPath);
        }
        else if (rc != VERR_PATH_ZERO_LENGTH || RTStrCmp(pszUri, "file://") != 0)
            RTTestIFailed("#%u: '%s'/%#x => %Rrc", i, pszUri, fPathStyle, rc);
    }
}


static void tstRTUriFileCreateEx(void)
{
    RTTestISub("RTUriFileCreateEx");
    for (size_t i = 0; i < RT_ELEMENTS(g_aCreateFileURIs); ++i)
    {
        uint32_t const     fPathStyle = g_aCreateFileURIs[i].fPathPathStyle;
        const char * const pszPath    = g_aCreateFileURIs[i].pszPath;
        char *pszUri = (char *)&pszUri;
        int rc = RTUriFileCreateEx(pszPath, fPathStyle, &pszUri, 0, NULL);
        if (RT_SUCCESS(rc))
        {
            if (g_aCreateFileURIs[i].pszCreatedUri)
            {
                if (strcmp(pszUri, g_aCreateFileURIs[i].pszCreatedUri) == 0)
                {
                    /** @todo check out the other variations of the API. */
                }
                else
                    RTTestIFailed("#%u: '%s'/%#x => '%s', expected '%s'",
                                  i, pszPath, fPathStyle, pszUri, g_aCreateFileURIs[i].pszCreatedUri);
            }
            else
                RTTestIFailed("#%u: bad testcase; pszCreatedUri is NULL\n", i);
            RTStrFree(pszUri);
        }
        else if (rc != VERR_INVALID_POINTER || pszPath != NULL)
            RTTestIFailed("#%u: '%s'/%#x => %Rrc", i, pszPath, fPathStyle, rc);
    }
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTUri", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

#define CHECK_STR_API(a_Call, a_pszExpected) \
        do { \
            char *pszTmp = a_Call; \
            if (a_pszExpected) \
            { \
                if (!pszTmp) \
                    RTTestIFailed("#%u: %s returns NULL, expected '%s'", i, #a_Call, a_pszExpected); \
                else if (strcmp(pszTmp, a_pszExpected)) \
                        RTTestIFailed("#%u: %s returns '%s', expected '%s'", i, #a_Call, pszTmp, a_pszExpected); \
            } \
            else if (pszTmp) \
                RTTestIFailed("#%u: %s returns '%s', expected NULL", i, #a_Call, pszTmp); \
            RTStrFree(pszTmp); \
        } while (0)

    RTTestISub("RTUriParse & RTUriParsed*");
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        RTURIPARSED Parsed;
        RTTESTI_CHECK_RC(rc = RTUriParse(g_aTests[i].pszUri, &Parsed), VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            CHECK_STR_API(RTUriParsedScheme(g_aTests[i].pszUri, &Parsed), g_aTests[i].pszScheme);
            CHECK_STR_API(RTUriParsedAuthority(g_aTests[i].pszUri, &Parsed), g_aTests[i].pszAuthority);
            CHECK_STR_API(RTUriParsedAuthorityUsername(g_aTests[i].pszUri, &Parsed), g_aTests[i].pszUsername);
            CHECK_STR_API(RTUriParsedAuthorityPassword(g_aTests[i].pszUri, &Parsed), g_aTests[i].pszPassword);
            CHECK_STR_API(RTUriParsedAuthorityHost(g_aTests[i].pszUri, &Parsed), g_aTests[i].pszHost);
            CHECK_STR_API(RTUriParsedPath(g_aTests[i].pszUri, &Parsed), g_aTests[i].pszPath);
            CHECK_STR_API(RTUriParsedQuery(g_aTests[i].pszUri, &Parsed), g_aTests[i].pszQuery);
            CHECK_STR_API(RTUriParsedFragment(g_aTests[i].pszUri, &Parsed), g_aTests[i].pszFragment);
            uint32_t uPort = RTUriParsedAuthorityPort(g_aTests[i].pszUri, &Parsed);
            if (uPort != g_aTests[i].uPort)
                RTTestIFailed("#%u: RTUriParsedAuthorityPort returns %#x, expected %#x", i, uPort, g_aTests[i].uPort);
        }
    }

    /* Creation */
    RTTestISub("RTUriCreate");
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
        CHECK_STR_API(RTUriCreate(g_aTests[i].pszScheme, g_aTests[i].pszAuthority, g_aTests[i].pszPath,
                                  g_aTests[i].pszQuery, g_aTests[i].pszFragment),
                      g_aTests[i].pszCreated ? g_aTests[i].pszCreated : g_aTests[i].pszUri);

#ifdef TSTRTURI_WITH_WINDOWS_REFERENCE_RESULTS
    tstWindowsReferenceResults();
#endif

    bool fSavedMayPanic = RTAssertSetMayPanic(false);
    bool fSavedQuiet    = RTAssertSetQuiet(true);

    tstRTUriFilePathEx();
    tstRTUriFileCreateEx();

    RTAssertSetMayPanic(fSavedMayPanic);
    RTAssertSetQuiet(fSavedQuiet);

    return RTTestSummaryAndDestroy(hTest);
}

