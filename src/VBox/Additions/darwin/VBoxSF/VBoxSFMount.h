/* $Id: VBoxSFMount.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - Darwin Shared Folders, mount interface.
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

#ifndef GA_INCLUDED_SRC_darwin_VBoxSF_VBoxSFMount_h
#define GA_INCLUDED_SRC_darwin_VBoxSF_VBoxSFMount_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/** The shared folders file system name.   */
#define VBOXSF_DARWIN_FS_NAME "vboxsf"

/**
 * Mount information that gets passed from userland on mount.
 */
typedef struct VBOXSFDRWNMOUNTINFO
{
    /** Magic value (VBOXSFDRWNMOUNTINFO_MAGIC).   */
    uint32_t    u32Magic;
    /** The shared folder name.   */
    char        szFolder[260];
} VBOXSFDRWNMOUNTINFO;
typedef VBOXSFDRWNMOUNTINFO *PVBOXSFDRWNMOUNTINFO;
/** Magic value for VBOXSFDRWNMOUNTINFO::u32Magic.   */
#define VBOXSFDRWNMOUNTINFO_MAGIC     UINT32_C(0xc001cafe)

#endif /* !GA_INCLUDED_SRC_darwin_VBoxSF_VBoxSFMount_h */

