/* $Id: VBoxZoneAccess.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxZoneAccess - Hack that keeps vboxdrv referenced for granting zone access, Solaris hosts.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <iprt/process.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define DEVICE_NAME     "/devices/pseudo/vboxdrv@0:vboxdrv"
#define DEVICE_NAME_USR "/devices/pseudo/vboxdrv@0:vboxdrvu"

int main(int argc, char *argv[])
{
    int hDevice = -1;
    int hDeviceUsr = -1;

    /* Check root permissions. */
    if (geteuid() != 0)
    {
        fprintf(stderr, "This program needs administrator privileges.\n");
        return -1;
    }

    /* Daemonize... */
    RTProcDaemonizeUsingFork(false /* fNoChDir */,
                             false /* fNoClose */,
                             NULL /* pszPidfile */);

    /* Open the device */
    hDevice = open(DEVICE_NAME, O_RDWR, 0);
    if (hDevice < 0)
    {
        fprintf(stderr, "Failed to open '%s'. errno=%d\n", DEVICE_NAME, errno);
        return errno;
    }

    /* Open the user device. */
    hDeviceUsr = open(DEVICE_NAME_USR, O_RDWR, 0);
    if (hDeviceUsr < 0)
    {
        fprintf(stderr, "Failed to open '%s'. errno=%d\n", DEVICE_NAME_USR, errno);
        close(hDevice);
        return errno;
    }

    /* Mark the file handle close on exec. */
    if (   fcntl(hDevice,    F_SETFD, FD_CLOEXEC) != 0
        || fcntl(hDeviceUsr, F_SETFD, FD_CLOEXEC) != 0)
    {
        fprintf(stderr, "Failed to set close on exec. errno=%d\n", errno);
        close(hDevice);
        close(hDeviceUsr);
        return errno;
    }

    /* Go to interruptible sleep for ~15 years... */
    /* avoid > 2^31 for Year 2038 32-bit overflow (Solaris 10) */
    sleep(500000000U);

    close(hDevice);
    close(hDeviceUsr);

    return 0;
}

