/* $Id: vboxfs_mount.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox File System Mount Helper, Solaris host.
 * Userspace mount wrapper that parses mount (or user-specified) options
 * and passes it to mount(2) syscall
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/vfs.h>
#include <sys/mount.h>

#include "vboxfs.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static char g_achOptBuf[MAX_MNTOPT_STR] = { '\0', };
static const int g_RetErr = 33;
static const int g_RetMagic = 2;
static const int g_RetOK = 0;

static void Usage(char *pszName)
{
    fprintf(stderr, "Usage: %s [OPTIONS] NAME MOUNTPOINT\n"
           "Mount the VirtualBox shared folder NAME from the host system to MOUNTPOINT.\n"
           "\n"
           "  -w                    mount the shared folder writable (the default)\n"
           "  -r                    mount the shared folder read-only\n"
           "  -o OPTION[,OPTION...] use the mount options specified\n"
           "\n", pszName);
    fprintf(stderr, "Available mount options are:\n"
           "\n"
           "     rw                 mount writable (the default)\n"
           "     ro                 mount read only\n"
           "     uid=UID            set the default file owner user id to UID\n"
           "     gid=GID            set the default file owner group id to GID\n");
    fprintf(stderr,
           "     dmode=MODE         override the mode for all directories (octal) to MODE\n"
           "     fmode=MODE         override the mode for all regular files (octal) to MODE\n"
           "     umask=UMASK        set the umask (bitmask of permissions not present) in (octal) UMASK\n"
           "     dmask=UMASK        set the umask applied to directories only in (octal) UMASK\n"
           "     fmask=UMASK        set the umask applied to regular files only in (octal) UMASK\n"
           "     stat_ttl=TTL       set the \"time to live\" (in ms) for the stat caches (default %d)\n", DEF_STAT_TTL_MS);
    fprintf(stderr,
           "     fsync              honor fsync calls instead of ignoring them\n"
           "     ttl=TTL            set the \"time to live\" to TID for the dentry\n"
           "     iocharset CHARSET  use the character set CHARSET for i/o operations (default utf8)\n"
           "     convertcp CHARSET  convert the shared folder name from the character set CHARSET to utf8\n\n"
           "Less common used options:\n"
           "     noexec,exec,nodev,dev,nosuid,suid\n");
    exit(1);
}

int main(int argc, char **argv)
{
    char *pszName      = NULL;
    char *pszSpecial   = NULL;
    char *pszMount     = NULL;
    char  achType[MAXFIDSZ];
    int   c = '?';
    int   rc = -1;
    int   parseError = 0;
    int   mntFlags = 0;
    int   quietFlag = 0;

    pszName = strrchr(argv[0], '/');
    pszName = pszName ? pszName + 1 : argv[0];
    snprintf(achType, sizeof(achType), "%s_%s", DEVICE_NAME, pszName);

    while ((c = getopt(argc, argv, "o:rmoQ")) != EOF)
    {
        switch (c)
        {
            case '?':
            {
                parseError = 1;
                break;
            }

            case 'q':
            {
                quietFlag = 1;
                break;
            }

            case 'r':
            {
                mntFlags |= MS_RDONLY;
                break;
            }

            case 'O':
            {
                mntFlags |= MS_OVERLAY;
                break;
            }

            case 'm':
            {
                mntFlags |= MS_NOMNTTAB;
                break;
            }

            case 'o':
            {
                if (strlcpy(g_achOptBuf, optarg, sizeof(g_achOptBuf)) >= sizeof(g_achOptBuf))
                {
                    fprintf(stderr, "%s: invalid argument: %s\n", pszName, optarg);
                    return g_RetMagic;
                }
                break;
            }

            default:
            {
                Usage(pszName);
                break;
            }
        }
    }

    if (   argc - optind != 2
        || parseError)
    {
        Usage(pszName);
    }

    pszSpecial = argv[argc - 2];
    pszMount = argv[argc - 1];

    rc = mount(pszSpecial, pszMount, mntFlags | MS_OPTIONSTR, DEVICE_NAME, NULL, 0, g_achOptBuf, sizeof(g_achOptBuf));
    if (rc)
    {
        fprintf(stderr, "mount:");
        perror(pszSpecial);
        return g_RetErr;
    }

    return g_RetOK;
}

