/* $Id: vboxmslnk.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Guest Additions Mouse Driver for Solaris: user space loader tool.
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

#include <VBox/version.h>
#include <iprt/buildconfig.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stropts.h>
#include <unistd.h>
#include <libgen.h>
#include <getopt.h>

#define VBOXMSLNK_MUXID_FILE    "/system/volatile/vboxmslnk.muxid"

static const char *g_pszProgName;


static void vboxmslnk_fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void) vfprintf(stderr, fmt, ap);
    if (fmt[strlen(fmt) - 1] != '\n')
        (void) fprintf(stderr, "  The error reported was: %s\n", strerror(errno));
    va_end(ap);

    exit(EXIT_FAILURE);
}

static void vboxmslnk_start(bool fNoLogo)
{
    /* Open our pointer integration driver (vboxms). */
    int hVBoxMS = open("/dev/vboxms", O_RDWR);
    if (hVBoxMS < 0)
        vboxmslnk_fatal("Failed to open /dev/vboxms - please make sure that the node exists and that\n"
                        "you have permission to open it.");

    /* Open the Solaris virtual mouse driver (consms). */
    int hConsMS = open("/dev/mouse", O_RDWR);
    if (hConsMS < 0)
        vboxmslnk_fatal("Failed to open /dev/mouse - please make sure that the node exists and that\n"
                        "you have permission to open it.");

    /* Link vboxms to consms from below.  What this means is that vboxms is
     * added to the list of input sources multiplexed by consms, and vboxms
     * will receive any control messages (such as information about guest
     * resolution changes) sent to consms.  The link can only be broken
     * explicitly using the connection ID returned from the IOCtl. */
    int idConnection = ioctl(hConsMS, I_PLINK, hVBoxMS);
    if (idConnection < 0)
        vboxmslnk_fatal("Failed to add /dev/vboxms (the pointer integration driver) to /dev/mouse\n"
                        "(the Solaris virtual master mouse).");

    (void) close(hVBoxMS);
    (void) close(hConsMS);

    if (!fNoLogo)
        (void) printf("Successfully enabled pointer integration.  Connection ID number to the\n"
                      "Solaris virtual master mouse is:\n");
    (void) printf("%d\n", idConnection);

    /* Save the connection ID (aka mux ID) so it can be retrieved later. */
    FILE *fp = fopen(VBOXMSLNK_MUXID_FILE, "w");
    if (fp == NULL)
        vboxmslnk_fatal("Failed to open %s for writing the connection ID.", VBOXMSLNK_MUXID_FILE);
    int rc = fprintf(fp, "%d\n", idConnection);
    if (rc <= 0)
        vboxmslnk_fatal("Failed to write the connection ID to %s.", VBOXMSLNK_MUXID_FILE);
    (void) fclose(fp);
}

static void vboxmslnk_stop()
{
   /* Open the Solaris virtual mouse driver (consms). */
    int hConsMS = open("/dev/mouse", O_RDWR);
    if (hConsMS < 0)
        vboxmslnk_fatal("Failed to open /dev/mouse - please make sure that the node exists and that\n"
                        "you have permission to open it.");

    /* Open the vboxmslnk.muxid file and retrieve the saved mux ID. */
    FILE *fp = fopen(VBOXMSLNK_MUXID_FILE, "r");
    if (fp == NULL)
        vboxmslnk_fatal("Failed to open %s for reading the connection ID.", VBOXMSLNK_MUXID_FILE);
    int idConnection;
    int rc = fscanf(fp, "%d\n", &idConnection);
    if (rc <= 0)
        vboxmslnk_fatal("Failed to read the connection ID from %s.", VBOXMSLNK_MUXID_FILE);
    (void) fclose(fp);
    (void) unlink(VBOXMSLNK_MUXID_FILE);

    /* Unlink vboxms from consms so that vboxms is able to be unloaded. */
    rc = ioctl(hConsMS, I_PUNLINK, idConnection);
    if (rc < 0)
        vboxmslnk_fatal("Failed to disconnect /dev/vboxms (the pointer integration driver) from\n"
                        "/dev/mouse (the Solaris virtual master mouse).");
    (void) close(hConsMS);
}

static void vboxmslnk_usage()
{
    (void) printf("Usage:\n"
           "  %s [--nologo] <--start | --stop>\n"
           "  %s [-V|--version]\n\n"
           "  -V|--version  print the tool version.\n"
           "  --nologo      do not display the logo text and only output the connection\n"
           "                ID number needed to disable pointer integration\n"
           "                again.\n"
           "  --start       Connect the VirtualBox pointer integration kernel module\n"
           "                to the Solaris mouse driver kernel module.\n"
           "  --stop        Disconnect the VirtualBox pointer integration kernel module\n"
           "                from the Solaris mouse driver kernel module.\n"
           "  -h|--help     display this help text.\n",
           g_pszProgName, g_pszProgName);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    bool fShowVersion = false, fNoLogo = false, fStart = false, fStop = false;
    int c;

    g_pszProgName = basename(argv[0]);

    static const struct option vboxmslnk_lopts[] = {
        {"version",     no_argument,            0, 'V'  },
        {"nologo",      no_argument,            0, 'n'  },
        {"start",       no_argument,            0, 's'  },
        {"stop",        no_argument,            0, 't'  },
        {"help",        no_argument,            0, 'h'  },
        { 0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "Vh", vboxmslnk_lopts, NULL)) != -1)
    {
        switch (c)
        {
        case 'V':
            fShowVersion = true;
            break;
        case 'n':
            fNoLogo = true;
            break;
        case 's':
            fStart = true;
            break;
        case 't':
            fStop = true;
            break;
        case 'h':
        default:
            vboxmslnk_usage();
        }
    }

    if (   (!fStart && !fStop && !fShowVersion)
        || (fStart && fStop)
        || (fShowVersion && (fNoLogo || fStart || fStop)))
        vboxmslnk_usage();

    if (fShowVersion)
    {
        (void) printf("%sr%u\n", VBOX_VERSION_STRING, RTBldCfgRevision());
        exit(EXIT_SUCCESS);
    }

    if (!fNoLogo)
        (void) printf(VBOX_PRODUCT
                      " Guest Additions utility for enabling Solaris pointer\nintegration Version "
                      VBOX_VERSION_STRING "\n"
                      "Copyright (C) " VBOX_C_YEAR " " VBOX_VENDOR "\n\n");

    if (fStart)
        vboxmslnk_start(fNoLogo);

    if (fStop)
        vboxmslnk_stop();

    exit(EXIT_SUCCESS);
}
