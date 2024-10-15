/* $Id: tstDir-2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Directory listing & filtering .
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

#include <iprt/dir.h>
#include <iprt/stream.h>
#include <iprt/err.h>
#include <iprt/initterm.h>

int main(int argc, char **argv)
{
    int rcRet = 0;
    RTR3InitExe(argc, &argv, 0);

    /*
     * Iterate arguments.
     */
    for (int i = 1; i < argc; i++)
    {
        /* open */
        RTDIR hDir;
        int rc = RTDirOpenFiltered(&hDir, argv[i], RTDIRFILTER_WINNT, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            for (;;)
            {
                RTDIRENTRY DirEntry;
                rc = RTDirRead(hDir, &DirEntry, NULL);
                if (RT_FAILURE(rc))
                    break;
                switch (DirEntry.enmType)
                {
                    case RTDIRENTRYTYPE_UNKNOWN:     RTPrintf("u"); break;
                    case RTDIRENTRYTYPE_FIFO:        RTPrintf("f"); break;
                    case RTDIRENTRYTYPE_DEV_CHAR:    RTPrintf("c"); break;
                    case RTDIRENTRYTYPE_DIRECTORY:   RTPrintf("d"); break;
                    case RTDIRENTRYTYPE_DEV_BLOCK:   RTPrintf("b"); break;
                    case RTDIRENTRYTYPE_FILE:        RTPrintf("-"); break;
                    case RTDIRENTRYTYPE_SYMLINK:     RTPrintf("l"); break;
                    case RTDIRENTRYTYPE_SOCKET:      RTPrintf("s"); break;
                    case RTDIRENTRYTYPE_WHITEOUT:    RTPrintf("w"); break;
                    default:
                        rcRet = 1;
                        RTPrintf("?");
                        break;
                }
                RTPrintf(" %#18llx  %3d %s\n", (uint64_t)DirEntry.INodeId,
                         DirEntry.cbName, DirEntry.szName);
            }

            if (rc != VERR_NO_MORE_FILES)
            {
                RTPrintf("tstDir-2: Enumeration failed! rc=%Rrc\n", rc);
                rcRet = 1;
            }

            /* close up */
            rc = RTDirClose(hDir);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstDir-2: Failed to close dir! rc=%Rrc\n", rc);
                rcRet = 1;
            }
        }
        else
        {
            RTPrintf("tstDir-2: Failed to open '%s', rc=%Rrc\n", argv[i], rc);
            rcRet = 1;
        }
    }

    return rcRet;
}

