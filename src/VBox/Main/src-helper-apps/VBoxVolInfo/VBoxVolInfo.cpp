/* $Id: VBoxVolInfo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Apps - VBoxVolInfo, Volume information tool.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iprt/errcore.h>

#include "libdevmapper.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/*
 * Looks up device name by id using /dev directory. Prints it to stdout.
 */
static void print_dev_name(dev_t devid)
{
    char path[PATH_MAX];
    struct dirent *de;
    DIR *dir = opendir("/dev");

    while ((de = readdir(dir)) != NULL)
    {
        struct stat st;
        snprintf(path, sizeof(path), "/dev/%s", de->d_name);
        if (!stat(path, &st))
            if (S_ISBLK(st.st_mode))
                if (devid == st.st_rdev)
                {
                    puts(de->d_name);
                    break;
                }
    }
    closedir(dir);
}


/*
 * Extracts logical volume dependencies via devmapper API and print them out.
 */
int main(int argc, char **argv)
{
    struct dm_task *dmtask;
    struct dm_info  dminfo;

    if (argc != 2)
    {
        fprintf(stderr, "USAGE: %s <volume_name>\n", argv[0]);
        return 1;
    }

    int vrc = RTDevmapperLoadLib();
    if (RT_FAILURE(vrc))
    {
        fprintf(stderr, "%s: libdevmapper library not found.  Service not available.\n", argv[0]);
        return 1;
    }

    dmtask = dm_task_create(DM_DEVICE_DEPS);
    if (!dmtask)
        return 2;

    if (dm_task_set_name(dmtask, argv[1]))
        if (dm_task_run(dmtask))
            if (dm_task_get_info(dmtask, &dminfo))
            {
                struct dm_deps *dmdeps = dm_task_get_deps(dmtask);
                if (dmdeps)
                {
                    unsigned i;
                    for (i = 0; i < dmdeps->count; ++i)
                        print_dev_name(dmdeps->device[i]);
                }
            }

    dm_task_destroy(dmtask);
    return 0;
}
