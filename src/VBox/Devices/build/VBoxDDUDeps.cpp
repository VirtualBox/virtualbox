/* $Id: VBoxDDUDeps.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDDU - For dragging in library objects.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <VBox/types.h>
#include <VBox/vd.h>
#ifdef VBOX_WITH_USB
# include <VBox/usblib.h>
# include <VBox/usbfilter.h>
# ifdef RT_OS_OS2
#  include <os2.h>
#  include <usbcalls.h>
# endif
#endif

/** Just a dummy global structure containing a bunch of
 * function pointers to code which is wanted in the link.
 */
struct CLANG11WEIRDNESS { PFNRT pfn; } g_apfnVBoxDDUDeps[] =
{
    { (PFNRT)VDInit },
    { (PFNRT)VDIfCreateVfsStream },
    { (PFNRT)VDIfCreateFromVfsStream },
    { (PFNRT)VDCreateVfsFileFromDisk },
    { (PFNRT)VDIfTcpNetInstDefaultCreate },
#ifdef VBOX_WITH_USB
    { (PFNRT)USBFilterInit },
    { (PFNRT)USBLibHashSerial },
# ifdef RT_OS_OS2
    { (PFNRT)UsbOpen },
# endif
# if defined(RT_OS_SOLARIS) || defined(RT_OS_WINDOWS) /* PORTME */
    { (PFNRT)USBLibInit },
# endif
#endif /* VBOX_WITH_USB */
    { NULL },
};

