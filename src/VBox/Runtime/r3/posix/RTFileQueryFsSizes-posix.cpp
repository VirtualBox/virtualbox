/* $Id: RTFileQueryFsSizes-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, RTFileFsQuerySizes, POSIX.
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
#define LOG_GROUP RTLOGGROUP_FILE

#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/statvfs.h>

#include <iprt/file.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/string.h>


RTR3DECL(int) RTFileQueryFsSizes(RTFILE hFile, PRTFOFF pcbTotal, RTFOFF *pcbFree,
                                 uint32_t *pcbBlock, uint32_t *pcbSector)
{
    struct statvfs StatVFS;
    RT_ZERO(StatVFS);
    if (fstatvfs(RTFileToNative(hFile), &StatVFS))
        return RTErrConvertFromErrno(errno);

    /*
     * Calc the returned values.
     */
    if (pcbTotal)
        *pcbTotal = (RTFOFF)StatVFS.f_blocks * StatVFS.f_frsize;
    if (pcbFree)
        *pcbFree = (RTFOFF)StatVFS.f_bavail * StatVFS.f_frsize;
    if (pcbBlock)
        *pcbBlock = StatVFS.f_frsize;
    /* no idea how to get the sector... */
    if (pcbSector)
        *pcbSector = 512;

    return VINF_SUCCESS;
}

