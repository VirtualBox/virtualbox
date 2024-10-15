/* $Id: mount.vboxsf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - Darwin Shared Folders, Mount Utility.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>

#include "VBoxSFMount.h"
#include <iprt/string.h>


static RTEXITCODE usage(const char *pszArg0)
{
    fprintf(stderr, "usage: %s [OPTIONS] <shared folder name> <mount point>\n", pszArg0);
    return RTEXITCODE_SYNTAX;
}


int main(int argc, char **argv)
{
    /*
     * Skip past parameters.
     */
    int c;
    while ((c = getopt(argc, argv, "o:")) != -1)
    {
        switch (c)
        {
            case 'o':
                break;
            default:
                return usage(argv[0]);
        }
    }

    /* Two arguments are rquired: <share name> and <mount point> */
    if (argc - optind != 2)
        return usage(argv[0]);
    const char * const pszFolder     = argv[optind++];
    const char * const pszMountPoint = argv[optind];

    /*
     * Check that the folder is within bounds and doesn't include any shady characters.
     */
    size_t cchFolder = strlen(pszFolder);
    if (   cchFolder < 1
        || cchFolder >= RT_SIZEOFMEMB(VBOXSFDRWNMOUNTINFO, szFolder)
        || strpbrk(pszFolder, "\\/") != NULL)
    {
        fprintf(stderr, "Invalid shared folder name '%s'!\n", pszFolder);
        return RTEXITCODE_FAILURE;
    }

    /*
     * Do the mounting.
     */
    VBOXSFDRWNMOUNTINFO MntInfo;
    RT_ZERO(MntInfo);
    MntInfo.u32Magic = VBOXSFDRWNMOUNTINFO_MAGIC;
    memcpy(MntInfo.szFolder, pszFolder, cchFolder);
    int rc = mount(VBOXSF_DARWIN_FS_NAME, pszMountPoint, 0, &MntInfo);
    if (rc == 0)
        return 0;

    fprintf(stderr, "error mounting '%s' at '%s': %s (%d)\n", pszFolder, pszMountPoint, strerror(errno), errno);
    return RTEXITCODE_FAILURE;
}
