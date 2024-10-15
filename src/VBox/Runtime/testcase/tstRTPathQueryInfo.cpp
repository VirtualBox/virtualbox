/* $Id: tstRTPathQueryInfo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTPathQueryInfoEx testcase
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
#include <iprt/path.h>
#include <iprt/initterm.h>
#include <iprt/errcore.h>
#include <iprt/stream.h>
#include <iprt/message.h>
#include <iprt/time.h>


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Iterate arguments.
     */
    RTEXITCODE      rcExit               = RTEXITCODE_SUCCESS;
    uint32_t        fFlags               = RTPATH_F_ON_LINK;
    RTFSOBJATTRADD  enmAdditionalAttribs = RTFSOBJATTRADD_NOTHING;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            for (int j = 1; argv[i][j]; j++)
            {
                switch (argv[i][j])
                {
                    case 'H':
                        fFlags = RTPATH_F_FOLLOW_LINK;
                        break;
                    case 'l':
                        enmAdditionalAttribs = RTFSOBJATTRADD_UNIX;
                        break;
                    default:
                        RTPrintf("Unknown option '%c' ignored!\n", argv[i][j]);
                        break;
                }
            }
        }
        else
        {
            RTFSOBJINFO ObjInfo;
            rc = RTPathQueryInfoEx(argv[i], &ObjInfo, enmAdditionalAttribs, fFlags);
            if (RT_SUCCESS(rc))
            {
                RTPrintf("  File: '%s'\n", argv[i]);
                RTPrintf("  Size: %'RTfoff  Allocated: %'RTfoff\n", ObjInfo.cbObject, ObjInfo.cbAllocated);

                RTPrintf("  Mode: ");
                RTFMODE fMode = ObjInfo.Attr.fMode;
                switch (fMode & RTFS_TYPE_MASK)
                {
                    case RTFS_TYPE_FIFO:        RTPrintf("f"); break;
                    case RTFS_TYPE_DEV_CHAR:    RTPrintf("c"); break;
                    case RTFS_TYPE_DIRECTORY:   RTPrintf("d"); break;
                    case RTFS_TYPE_DEV_BLOCK:   RTPrintf("b"); break;
                    case RTFS_TYPE_FILE:        RTPrintf("-"); break;
                    case RTFS_TYPE_SYMLINK:     RTPrintf("l"); break;
                    case RTFS_TYPE_SOCKET:      RTPrintf("s"); break;
                    case RTFS_TYPE_WHITEOUT:    RTPrintf("w"); break;
                    default:
                        rcExit = RTEXITCODE_FAILURE;
                        RTPrintf("?");
                        break;
                }
                /** @todo sticy bits++ */
                RTPrintf("%c%c%c",
                         fMode & RTFS_UNIX_IRUSR ? 'r' : '-',
                         fMode & RTFS_UNIX_IWUSR ? 'w' : '-',
                         fMode & RTFS_UNIX_IXUSR ? 'x' : '-');
                RTPrintf("%c%c%c",
                         fMode & RTFS_UNIX_IRGRP ? 'r' : '-',
                         fMode & RTFS_UNIX_IWGRP ? 'w' : '-',
                         fMode & RTFS_UNIX_IXGRP ? 'x' : '-');
                RTPrintf("%c%c%c",
                         fMode & RTFS_UNIX_IROTH ? 'r' : '-',
                         fMode & RTFS_UNIX_IWOTH ? 'w' : '-',
                         fMode & RTFS_UNIX_IXOTH ? 'x' : '-');

                RTPrintf("  Attributes: %c%c%c%c%c%c%c%c%c%c%c%c%c%c",
                         fMode & RTFS_DOS_READONLY          ? 'R' : '-',
                         fMode & RTFS_DOS_HIDDEN            ? 'H' : '-',
                         fMode & RTFS_DOS_SYSTEM            ? 'S' : '-',
                         fMode & RTFS_DOS_DIRECTORY         ? 'D' : '-',
                         fMode & RTFS_DOS_ARCHIVED          ? 'A' : '-',
                         fMode & RTFS_DOS_NT_DEVICE         ? 'd' : '-',
                         fMode & RTFS_DOS_NT_NORMAL         ? 'N' : '-',
                         fMode & RTFS_DOS_NT_TEMPORARY      ? 'T' : '-',
                         fMode & RTFS_DOS_NT_SPARSE_FILE    ? 'P' : '-',
                         fMode & RTFS_DOS_NT_REPARSE_POINT  ? 'J' : '-',
                         fMode & RTFS_DOS_NT_COMPRESSED     ? 'C' : '-',
                         fMode & RTFS_DOS_NT_OFFLINE        ? 'O' : '-',
                         fMode & RTFS_DOS_NT_NOT_CONTENT_INDEXED ? 'I' : '-',
                         fMode & RTFS_DOS_NT_ENCRYPTED      ? 'E' : '-');
                RTPrintf("\n");

                if (enmAdditionalAttribs == RTFSOBJATTRADD_UNIX)
                {
                    RTPrintf(" Inode: %#llx  InodeDevice: %#x  Links: %u\n",
                             ObjInfo.Attr.u.Unix.INodeId,
                             ObjInfo.Attr.u.Unix.INodeIdDevice,
                             ObjInfo.Attr.u.Unix.cHardlinks);
                    RTPrintf("   Uid: %d  Gid: %d\n",
                             ObjInfo.Attr.u.Unix.uid,
                             ObjInfo.Attr.u.Unix.gid);
                }

                char szTmp[80];
                RTPrintf(" Birth: %s\n", RTTimeSpecToString(&ObjInfo.BirthTime, szTmp, sizeof(szTmp)));
                RTPrintf("Access: %s\n", RTTimeSpecToString(&ObjInfo.AccessTime, szTmp, sizeof(szTmp)));
                RTPrintf("Modify: %s\n", RTTimeSpecToString(&ObjInfo.ModificationTime, szTmp, sizeof(szTmp)));
                RTPrintf("Change: %s\n", RTTimeSpecToString(&ObjInfo.ChangeTime, szTmp, sizeof(szTmp)));

            }
            else
            {
                RTPrintf("RTPathQueryInfoEx(%s,,%d,%#x) -> %Rrc\n", argv[i], enmAdditionalAttribs, fFlags, rc);
                rcExit = RTEXITCODE_FAILURE;
            }
        }
    }

    return rcExit;
}

