/* $Id: vboxfs_vfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox File System for Solaris Guests, VFS header.
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

#ifndef GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_vfs_h
#define GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_vfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Shared Folders filesystem per-mount data structure.
 */
typedef struct sffs_data {
	vfs_t		*sf_vfsp;	/* filesystem's vfs struct */
	vnode_t		*sf_rootnode;	/* of vnode of the root directory */
	int  		sf_stat_ttl;	/* ttl for stat caches (in ms) */
	int  		sf_fsync;	/* whether to honor fsync or not */
	char		*sf_share_name;
	char 		*sf_mntpath;	/* name of mount point */
	sfp_mount_t	*sf_handle;
	uint64_t	sf_ino;		/* per FS ino generator */
} sffs_data_t;

/*
 * Workaround for older Solaris versions which called map_addr()/choose_addr()/
 * map_addr_proc() with an 'alignment' argument that was removed in Solaris
 * 11.4.
 */
typedef struct VBoxVFS_SolAddrMap
{
    union
    {
        void *(*pfnSol_map_addr)          (caddr_t *, size_t, offset_t, uint_t);
        void *(*pfnSol_map_addr_old)      (caddr_t *, size_t, offset_t, int, uint_t);
    } MapAddr;

    union
    {
        int (*pfnSol_choose_addr)       (struct as *, caddr_t *, size_t, offset_t, uint_t);
        int (*pfnSol_choose_addr_old)   (struct as *, caddr_t *, size_t, offset_t, int, uint_t);
    } ChooseAddr;
} VBoxVFS_SolAddrMap;
typedef VBoxVFS_SolAddrMap *pVBoxVFS_SolAddrMap;

extern bool                     g_fVBoxVFS_SolOldAddrMap;
extern VBoxVFS_SolAddrMap       g_VBoxVFS_SolAddrMap;

#ifdef	__cplusplus
}
#endif

#endif /* !GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_vfs_h */

