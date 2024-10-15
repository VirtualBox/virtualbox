/* $Id: fileioutils-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, common NT helpers.
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
#include <iprt/nt/nt.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include "internal/file.h"



/**
 * Helper for converting RTFILE_O_XXX to the various NtCreateFile flags.
 *
 * @returns IPRT status code
 * @param   fOpen               The RTFILE_O_XXX flags to convert.
 * @param   pfDesiredAccess     Where to return the desired access mask.
 * @param   pfObjAttribs        Where to return the NT object attributes.
 * @param   pfFileAttribs       Where to return the file attributes (create).
 * @param   pfShareAccess       Where to return the file sharing access mask.
 * @param   pfCreateDisposition Where to return the file create disposition.
 * @param   pfCreateOptions     Where to return the file open/create options.
 */
DECLHIDDEN(int) rtFileNtValidateAndConvertFlags(uint64_t fOpen, uint32_t *pfDesiredAccess, uint32_t *pfObjAttribs,
                                                uint32_t *pfFileAttribs, uint32_t *pfShareAccess, uint32_t *pfCreateDisposition,
                                                uint32_t *pfCreateOptions)
{
    /*
     * Merge forced open flags and validate them.
     */
    int rc = rtFileRecalcAndValidateFlags(&fOpen);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Determine disposition, access, share mode, creation flags, and security attributes
     * for the CreateFile API call.
     */
    ULONG fCreateDisposition;
    switch (fOpen & RTFILE_O_ACTION_MASK)
    {
        case RTFILE_O_OPEN:
            fCreateDisposition = fOpen & RTFILE_O_TRUNCATE ? FILE_OVERWRITE : FILE_OPEN;
            break;
        case RTFILE_O_OPEN_CREATE:
            fCreateDisposition = FILE_OPEN_IF;
            break;
        case RTFILE_O_CREATE:
            fCreateDisposition = FILE_CREATE;
            break;
        case RTFILE_O_CREATE_REPLACE:
            fCreateDisposition = FILE_SUPERSEDE;
            break;
        default:
            AssertMsgFailed(("Impossible fOpen=%#llx\n", fOpen));
            return VERR_INVALID_PARAMETER;
    }

    ACCESS_MASK fDesiredAccess;
    switch (fOpen & RTFILE_O_ACCESS_MASK)
    {
        case RTFILE_O_READ:
            fDesiredAccess = FILE_GENERIC_READ; /* RTFILE_O_APPEND is ignored. */
            break;
        case RTFILE_O_WRITE:
            fDesiredAccess = fOpen & RTFILE_O_APPEND
                           ? FILE_GENERIC_WRITE & ~FILE_WRITE_DATA
                           : FILE_GENERIC_WRITE;
            break;
        case RTFILE_O_READWRITE:
            fDesiredAccess = fOpen & RTFILE_O_APPEND
                           ? FILE_GENERIC_READ | (FILE_GENERIC_WRITE & ~FILE_WRITE_DATA)
                           : FILE_GENERIC_READ | FILE_GENERIC_WRITE;
            break;
        case RTFILE_O_ATTR_ONLY:
            if (fOpen & RTFILE_O_ACCESS_ATTR_MASK)
            {
                fDesiredAccess = 0;
                break;
            }
            RT_FALL_THRU();
        default:
            AssertMsgFailed(("Impossible fOpen=%#llx\n", fOpen));
            return VERR_INVALID_PARAMETER;
    }
    if (fCreateDisposition == FILE_OVERWRITE)
        /* Required for truncating the file (see MSDN), it is *NOT* part of FILE_GENERIC_WRITE. */
        fDesiredAccess |= GENERIC_WRITE;

    /* RTFileSetMode needs following rights as well. */
    switch (fOpen & RTFILE_O_ACCESS_ATTR_MASK)
    {
        case RTFILE_O_ACCESS_ATTR_READ:      fDesiredAccess |= FILE_READ_ATTRIBUTES  | SYNCHRONIZE; break;
        case RTFILE_O_ACCESS_ATTR_WRITE:     fDesiredAccess |= FILE_WRITE_ATTRIBUTES | SYNCHRONIZE; break;
        case RTFILE_O_ACCESS_ATTR_READWRITE: fDesiredAccess |= FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE; break;
        default:
            /* Attributes access is the same as the file access. */
            switch (fOpen & RTFILE_O_ACCESS_MASK)
            {
                case RTFILE_O_READ:          fDesiredAccess |= FILE_READ_ATTRIBUTES  | SYNCHRONIZE; break;
                case RTFILE_O_WRITE:         fDesiredAccess |= FILE_WRITE_ATTRIBUTES | SYNCHRONIZE; break;
                case RTFILE_O_READWRITE:     fDesiredAccess |= FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE; break;
                default:
                    AssertMsgFailed(("Impossible fOpen=%#llx\n", fOpen));
                    return VERR_INVALID_PARAMETER;
            }
    }

    ULONG fShareAccess;
    switch (fOpen & RTFILE_O_DENY_MASK)
    {
        case RTFILE_O_DENY_NONE:                                fShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
        case RTFILE_O_DENY_READ:                                fShareAccess = FILE_SHARE_WRITE; break;
        case RTFILE_O_DENY_WRITE:                               fShareAccess = FILE_SHARE_READ; break;
        case RTFILE_O_DENY_READWRITE:                           fShareAccess = 0; break;

        case RTFILE_O_DENY_NOT_DELETE:                          fShareAccess = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE; break;
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_READ:     fShareAccess = FILE_SHARE_DELETE | FILE_SHARE_WRITE; break;
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_WRITE:    fShareAccess = FILE_SHARE_DELETE | FILE_SHARE_READ; break;
        case RTFILE_O_DENY_NOT_DELETE | RTFILE_O_DENY_READWRITE:fShareAccess = FILE_SHARE_DELETE; break;
        default:
            AssertMsgFailed(("Impossible fOpen=%#llx\n", fOpen));
            return VERR_INVALID_PARAMETER;
    }

    ULONG fObjAttribs = 0;
    if (fOpen & RTFILE_O_INHERIT)
        fObjAttribs = OBJ_INHERIT;

    ULONG fCreateOptions = FILE_NON_DIRECTORY_FILE;
    if (fOpen & RTFILE_O_WRITE_THROUGH)
        fCreateOptions |= FILE_WRITE_THROUGH;
    if (!(fOpen & RTFILE_O_ASYNC_IO))
        fCreateOptions |= FILE_SYNCHRONOUS_IO_NONALERT;
    if (fOpen & RTFILE_O_NO_CACHE)
    {
        fCreateOptions |= FILE_NO_INTERMEDIATE_BUFFERING;
        fDesiredAccess &= ~FILE_APPEND_DATA;
    }

    /*
     * Done.
     */
    *pfDesiredAccess        = fDesiredAccess;
    *pfObjAttribs           = fObjAttribs;
    *pfFileAttribs          = FILE_ATTRIBUTE_NORMAL;
    *pfShareAccess          = fShareAccess;
    *pfCreateDisposition    = fCreateDisposition;
    *pfCreateOptions        = fCreateOptions;
    return VINF_SUCCESS;
}

