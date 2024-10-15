/* $Id: vboxvfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Description.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_freebsd_vboxvfs_vboxvfs_h
#define GA_INCLUDED_SRC_freebsd_vboxvfs_vboxvfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define VBOXVFS_VFSNAME "vboxvfs"
#define VBOXVFS_VERSION 1

#define MAX_HOST_NAME 256
#define MAX_NLS_NAME 32

struct vboxvfs_mount_info {
    char name[MAX_HOST_NAME];
    char nls_name[MAX_NLS_NAME];
    int uid;
    int gid;
    int ttl;
};

#ifdef _KERNEL

#include <VBox/VBoxGuestLibSharedFolders.h>
#include <sys/mount.h>
#include <sys/vnode.h>

struct vboxvfsmount {
    uid_t           uid;
    gid_t           gid;
    mode_t          file_mode;
    mode_t          dir_mode;
    struct mount   *mp;
    struct ucred   *owner;
    u_int           flags;
    long            nextino;
    int             caseopt;
    int             didrele;
};

/* structs - stolen from the linux shared module code */
struct sf_glob_info {
    VBGLSFMAP map;
/*    struct nls_table *nls;*/
    int ttl;
    int uid;
    int gid;
    struct vnode *vnode_root;
};

struct sf_inode_info {
    SHFLSTRING *path;
    int force_restat;
};

#if 0
struct sf_dir_info {
    struct list_head info_list;
};
#endif

struct sf_dir_buf {
    size_t nb_entries;
    size_t free_bytes;
    size_t used_bytes;
    void *buf;
#if 0
   struct list_head head;
#endif
};

struct sf_reg_info {
    SHFLHANDLE handle;
};

#endif  /* KERNEL */

#endif /* !GA_INCLUDED_SRC_freebsd_vboxvfs_vboxvfs_h */

