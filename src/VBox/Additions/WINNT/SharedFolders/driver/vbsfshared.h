/* $Id: vbsfshared.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders FSD - Definitions shared with the network provider dll.
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

#ifndef GA_INCLUDED_SRC_WINNT_SharedFolders_driver_vbsfshared_h
#define GA_INCLUDED_SRC_WINNT_SharedFolders_driver_vbsfshared_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** The network provider name for shared folders. */
#define MRX_VBOX_PROVIDER_NAME_U                L"VirtualBox Shared Folders"

/** The filesystem name for shared folders. */
#define MRX_VBOX_FILESYS_NAME_U                 L"VBoxSharedFolderFS"

/** The redirector device name. */
#define DD_MRX_VBOX_FS_DEVICE_NAME_U            L"\\Device\\VBoxMiniRdr"

/** Volume label prefix. */
#define VBOX_VOLNAME_PREFIX                     L"VBOX_"
/** Size of volume label prefix. */
#define VBOX_VOLNAME_PREFIX_SIZE                (sizeof(VBOX_VOLNAME_PREFIX) - sizeof(VBOX_VOLNAME_PREFIX[0]))

/** NT path of the symbolic link, which is used by the user mode dll to
 * open the FSD. */
#define DD_MRX_VBOX_USERMODE_SHADOW_DEV_NAME_U  L"\\??\\VBoxMiniRdrDN"
/** Win32 path of the symbolic link, which is used by the user mode dll
 * to open the FSD. */
#define DD_MRX_VBOX_USERMODE_DEV_NAME_U         L"\\\\.\\VBoxMiniRdrDN"

/** @name IOCTL_MRX_VBOX_XXX - VBoxSF IOCTL codes.
 * @{  */
#define IOCTL_MRX_VBOX_ADDCONN          CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_GETCONN          CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 101, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_DELCONN          CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 102, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_GETLIST          CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_GETGLOBALLIST    CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 104, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_GETGLOBALCONN    CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 105, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_START            CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 106, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MRX_VBOX_STOP             CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 107, METHOD_BUFFERED, FILE_ANY_ACCESS)
/** @} */

#endif /* !GA_INCLUDED_SRC_WINNT_SharedFolders_driver_vbsfshared_h */
