/** $Id: VBoxReplaceDll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxReplaceDll - helper for replacing a dll when it's in use by the system
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
#define INCL_BASE
#include <os2.h>
#include <stdio.h>
#include <string.h>

#include <VBox/version.h>


static int usage(const char *argv0)
{
    char *psz1 = strrchr(argv0, '\\');
    if (psz1)
        argv0 = psz1 + 1;
    psz1 = strrchr(argv0, '/');
    if (psz1)
        argv0 = psz1 + 1;
    psz1 = strrchr(argv0, ':');
    if (psz1)
        argv0 = psz1 + 1;

    printf("Usage: %s <dll1> [dll2 ...[dllN]]\n"
           "\n"
           "Tells the kernel to cache the specified DLLs in memory and close the\n"
           "files on disk, allowing new DLL versions to be installed.\n"
           "\n"
           "Copyright (C) 2013-" VBOX_C_YEAR " Oracle Corporation\n",
           argv0);
    return 0;
}

int main(int argc, char **argv)
{
    int fOptions   = 1;
    int cProcessed = 0;
    int i;
    for (i = 1; i < argc; i++)
    {
        if (   fOptions
            && argv[i][0] == '-')
        {
            if (!strcmp(argv[i], "--"))
                fOptions = 0;
            else if (   !strcmp(argv[i], "--help")
                     || !strcmp(argv[i], "-help")
                     || !strcmp(argv[i], "-h")
                     || !strcmp(argv[i], "-?") )
                return usage(argv[0]);
            else if (   !strcmp(argv[i], "--version")
                     || !strcmp(argv[i], "-V") )
            {
                printf("$Revision: 106320 $\n");
                return 0;
            }
            else
            {
                fprintf(stderr, "syntax error: Invalid option '%s'!\n", argv[i]);
                return 2;
            }
        }
        else
        {
            /*
             * Replace the specified DLL.
             */
            APIRET rc = DosReplaceModule((PCSZ)argv[i], NULL, NULL);
            if (rc == NO_ERROR)
                printf("info: Successfully cached '%s'.\n", argv[i]);
            else
            {
                fprintf(stderr, "error: DosReplaceModule failed with rc=%lu on  '%s'.\n", rc, argv[i]);
                return 1;
            }
            cProcessed++;
        }
    }

    if (cProcessed == 0)
    {
        fprintf(stderr, "syntax error: No DLLs specified. (Consult --help for usage.)\n");
        return 1;
    }

    return 0;
}

