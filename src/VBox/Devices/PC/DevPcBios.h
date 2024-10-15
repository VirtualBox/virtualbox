/* $Id: DevPcBios.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPcBios - PC BIOS Device, header shared with the BIOS code.
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

#ifndef VBOX_INCLUDED_SRC_PC_DevPcBios_h
#define VBOX_INCLUDED_SRC_PC_DevPcBios_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @def VBOX_DMI_TABLE_BASE */
#define VBOX_DMI_TABLE_BASE         0xe1000
#define VBOX_DMI_TABLE_VER          0x25

/** def VBOX_DMI_TABLE_SIZE
 *
 * The size should be at least 16-byte aligned for a proper alignment of
 * the MPS table.
 */
#define VBOX_DMI_TABLE_SIZE         768

/** def VBOX_DMI_TABLE_SIZE
 *
 * The size should be at least 16-byte aligned for a proper alignment of
 * the MPS table.
 */
#define VBOX_DMI_HDR_SIZE           32

/** @def VBOX_LANBOOT_SEG
 *
 * Should usually start right after the DMI BIOS page
 */
#define VBOX_LANBOOT_SEG            0xe200

#define VBOX_SMBIOS_MAJOR_VER       2
#define VBOX_SMBIOS_MINOR_VER       5
#define VBOX_SMBIOS_MAXSS           0xff   /* Not very accurate */

#endif /* !VBOX_INCLUDED_SRC_PC_DevPcBios_h */
