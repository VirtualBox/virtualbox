/* $Id: nocrt-fstat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - fstat().
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define IPRT_NO_CRT_FOR_3RD_PARTY
#include "internal/nocrt.h"
#include <iprt/nocrt/sys/stat.h>
#include <iprt/nocrt/errno.h>
#include <iprt/errcore.h>
#include <iprt/file.h>


#undef fstat
int RT_NOCRT(fstat)(int fd, struct RT_NOCRT(stat) *pStat)
{
    RTFILE hFile;
    int rc = RTFileFromNative(&hFile, fd);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO ObjInfo;
        rc = RTFileQueryInfo(hFile, &ObjInfo, RTFSOBJATTRADD_UNIX);
        if (RT_SUCCESS(rc))
        {
            pStat->st_ino       = ObjInfo.Attr.u.Unix.INodeId;
            pStat->st_dev       = ObjInfo.Attr.u.Unix.INodeIdDevice;
            pStat->st_rdev      = ObjInfo.Attr.u.Unix.Device;
            pStat->st_mode      = ObjInfo.Attr.fMode;
            pStat->st_link      = ObjInfo.Attr.u.Unix.cHardlinks;
            pStat->st_uid       = ObjInfo.Attr.u.Unix.uid;
            pStat->st_gid       = ObjInfo.Attr.u.Unix.gid;
            pStat->st_size      = ObjInfo.cbObject;
            pStat->st_blocks    = (ObjInfo.cbObject + 511) / 512;
            pStat->st_blksize   = 16384; /* whatever */
            pStat->st_birthtime = RTTimeSpecGetSeconds(&ObjInfo.BirthTime);
            pStat->st_ctime     = RTTimeSpecGetSeconds(&ObjInfo.ChangeTime);
            pStat->st_mtime     = RTTimeSpecGetSeconds(&ObjInfo.ModificationTime);
            pStat->st_atime     = RTTimeSpecGetSeconds(&ObjInfo.AccessTime);
            return 0;
        }
    }
    errno = RTErrConvertToErrno(rc);
    return -1;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(fstat);

