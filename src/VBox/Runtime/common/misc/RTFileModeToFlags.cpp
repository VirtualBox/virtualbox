/* $Id: RTFileModeToFlags.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileModeToFlags.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include "internal/iprt.h"


RTR3DECL(int) RTFileModeToFlags(const char *pszMode, uint64_t *pfMode)
{
    AssertPtrReturn(pszMode, VERR_INVALID_POINTER);
    AssertPtrReturn(pfMode, VERR_INVALID_POINTER);

    const char *pszCur = pszMode;
    if (*pszCur == '\0')
        return VERR_INVALID_PARAMETER;

    uint64_t    fMode  = 0;
    char        chPrev = '\0';
    while (    pszCur
           && *pszCur != '\0')
    {
        bool fSkip = false;
        switch (*pszCur)
        {
            /* Opens an existing file for writing and places the
             * file pointer at the end of the file. The file is
             * created if it does not exist. */
            case 'a':
                if ((fMode & RTFILE_O_ACTION_MASK) == 0)
                    fMode |= RTFILE_O_OPEN_CREATE
                           | RTFILE_O_WRITE
                           | RTFILE_O_APPEND;
                else
                    return VERR_INVALID_PARAMETER;
                break;

            case 'b': /* Binary mode. */
                /* Just skip as being valid. */
                fSkip = true;
                break;

            /* Creates a file or open an existing one for
             * writing only. The file pointer will be placed
             * at the beginning of the file.*/
            case 'c':
                if ((fMode & RTFILE_O_ACTION_MASK) == 0)
                    fMode |= RTFILE_O_OPEN_CREATE
                           | RTFILE_O_WRITE;
                else
                    return VERR_INVALID_PARAMETER;
                break;

            /* Opens an existing file for reading and places the
             * file pointer at the beginning of the file. If the
             * file does not exist an error will be returned. */
            case 'r':
                if ((fMode & RTFILE_O_ACTION_MASK) == 0)
                    fMode |=  RTFILE_O_OPEN
                           | RTFILE_O_READ;
                else
                    return VERR_INVALID_PARAMETER;
                break;

            case 't': /* Text mode. */
                /* Just skip as being valid. */
                fSkip = true;
                break;

            /* Creates a new file or replaces an existing one
             * for writing. Places the file pointer at the beginning.
             * An existing file will be truncated to 0 bytes. */
            case 'w':
                if ((fMode & RTFILE_O_ACTION_MASK) == 0)
                    fMode |= RTFILE_O_CREATE_REPLACE
                           | RTFILE_O_WRITE
                           | RTFILE_O_TRUNCATE;
                else
                    return VERR_INVALID_PARAMETER;
                break;

            /* Creates a new file and opens it for writing. Places
             * the file pointer at the beginning. If the file
             * exists an error will be returned. */
            case 'x':
                if ((fMode & RTFILE_O_ACTION_MASK) == 0)
                    fMode |= RTFILE_O_CREATE
                           | RTFILE_O_WRITE;
                else
                    return VERR_INVALID_PARAMETER;
                break;

            case '+':
            {
                switch (chPrev)
                {
                    case 'a':
                    case 'c':
                    case 'w':
                    case 'x':
                        /* Also open / create file with read access. */
                        fMode |= RTFILE_O_READ;
                        break;

                    case 'r':
                        /* Also open / create file with write access. */
                        fMode |= RTFILE_O_WRITE;
                        break;

                    case 'b':
                    case 't':
                        /* Silently eat skipped parameters. */
                        fSkip = true;
                        break;

                    case 0: /* No previous character yet. */
                    case '+':
                        /* Eat plusses which don't belong to a command. */
                        fSkip = true;
                        break;

                    default:
                        return VERR_INVALID_PARAMETER;
                }

                break;
            }

            default:
                return VERR_INVALID_PARAMETER;
        }

        if (!fSkip)
            chPrev = *pszCur;
        pszCur++;
    }

    /* No action mask set? */
    if ((fMode & RTFILE_O_ACTION_MASK) == 0)
        return VERR_INVALID_PARAMETER;

    /** @todo Handle sharing mode */
    fMode |= RTFILE_O_DENY_NONE;

    /* Return. */
    *pfMode = fMode;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTFileModeToFlags);


RTR3DECL(int) RTFileModeToFlagsEx(const char *pszAccess, const char *pszDisposition,
                                  const char *pszSharing, uint64_t *pfMode)
{
    AssertPtrReturn(pszAccess, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDisposition, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pszSharing, VERR_INVALID_POINTER);
    AssertPtrReturn(pfMode, VERR_INVALID_POINTER);

    const char *pszCur = pszAccess;
    if (*pszCur == '\0')
        return VERR_INVALID_PARAMETER;

    /*
     * Handle access mode.
     */
    uint64_t fMode  = 0;
    char     chPrev = '\0';
    while (    pszCur
           && *pszCur != '\0')
    {
        bool fSkip = false;
        switch (*pszCur)
        {
            case 'b': /* Binary mode. */
                /* Just skip as being valid. */
                fSkip = true;
                break;

            case 'r': /* Read. */
                fMode |= RTFILE_O_READ;
                break;

            case 't': /* Text mode. */
                /* Just skip as being valid. */
                fSkip = true;
                break;

            case 'w': /* Write. */
                fMode |= RTFILE_O_WRITE;
                break;

            case 'a': /* Append. */
                fMode |= RTFILE_O_WRITE | RTFILE_O_APPEND;
                break;

            case '+':
            {
                switch (chPrev)
                {
                    case 'w':
                    case 'a':
                        /* Also use read access in write mode. */
                        fMode |= RTFILE_O_READ;
                        break;

                    case 'r':
                        /* Also use write access in read mode. */
                        fMode |= RTFILE_O_WRITE;
                        break;

                    case 'b':
                    case 't':
                        /* Silently eat skipped parameters. */
                        fSkip = true;
                        break;

                    case 0: /* No previous character yet. */
                    case '+':
                        /* Eat plusses which don't belong to a command. */
                        fSkip = true;
                        break;

                    default:
                        return VERR_INVALID_PARAMETER;
                }

                break;
            }

            default:
                return VERR_INVALID_PARAMETER;
        }

        if (!fSkip)
            chPrev = *pszCur;
        pszCur++;
    }

    /*
     * Handle disposition.
     */
    pszCur = pszDisposition;

    /* Create a new file, always, overwrite an existing file. */
    if (   !RTStrCmp(pszCur, "ca")
        || !RTStrCmp(pszCur, "create-replace"))
        fMode |= RTFILE_O_CREATE_REPLACE;
    /* Create a new file if it does not exist, fail if exist. */
    else if (   !RTStrCmp(pszCur, "ce")
             || !RTStrCmp(pszCur, "create"))
        fMode |= RTFILE_O_CREATE;
    /* Open existing file, create file if does not exist. */
    else if (   !RTStrCmp(pszCur, "oc")
             || !RTStrCmp(pszCur, "open-create"))
        fMode |= RTFILE_O_OPEN_CREATE;
    /* Open existing file and place the file pointer at the end of the file, if
     * opened with write access. Create the file if does not exist.
     * Note! This mode is ill conceived as the appending is a accesss mode not open disposition. */
    else if (   !RTStrCmp(pszCur, "oa")
             || !RTStrCmp(pszCur, "open-append"))
        fMode |= RTFILE_O_OPEN_CREATE | RTFILE_O_APPEND;
    /* Open existing, fail if does not exist. */
    else if (   !RTStrCmp(pszCur, "oe")
             || !RTStrCmp(pszCur, "open"))
        fMode |= RTFILE_O_OPEN;
    /* Open and truncate existing, fail of not exist. */
    else if (   !RTStrCmp(pszCur, "ot")
             || !RTStrCmp(pszCur, "open-truncate"))
        fMode |= RTFILE_O_OPEN | RTFILE_O_TRUNCATE;
    else
        return VERR_INVALID_PARAMETER;

    /* No action mask set? */
    if ((fMode & RTFILE_O_ACTION_MASK) == 0)
        return VERR_INVALID_PARAMETER;

    /*
     * Sharing mode.
     */
    if (!pszSharing || !*pszSharing)
        fMode |= RTFILE_O_DENY_NONE;
    else
    {
        do
        {
            if (pszSharing[0] == 'n')
            {
                if (pszSharing[1] == 'r') /* nr (no other readers) */
                {
                    if (pszSharing[2] == 'w') /* nrw (no other readers or writers) */
                    {
                        fMode |= RTFILE_O_DENY_READWRITE;
                        pszSharing += 3;
                    }
                    else
                    {
                        fMode |= RTFILE_O_DENY_READ;
                        pszSharing += 2;
                    }
                }
                else if (pszSharing[1] == 'w') /* nw (no other writers) */
                {
                    fMode |= RTFILE_O_DENY_WRITE;
                    pszSharing += 2;
                }
                else
                    return VERR_INVALID_PARAMETER;
            }
            else if (pszSharing[0] == 'd') /* d (don't deny delete) */
            {
                fMode |= RTFILE_O_DENY_WRITE;
                pszSharing++;
            }
            else
                return VERR_INVALID_PARAMETER;
        } while (*pszSharing != '\0');
    }

    /* Return. */
    *pfMode = fMode;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTFileModeToFlagsEx);

