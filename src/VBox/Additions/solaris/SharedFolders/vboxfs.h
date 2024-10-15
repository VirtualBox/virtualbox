/* $Id: vboxfs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox File System Driver for Solaris Guests, Internal Header.
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

#ifndef GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_h
#define GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_HOST_NAME   256
#define MAX_NLS_NAME    32
/** Default stat cache ttl (in ms) */
#define DEF_STAT_TTL_MS 200

/** The module name. */
#define DEVICE_NAME              "vboxfs"

#ifdef _KERNEL

#include <VBox/VBoxGuestLibSharedFolders.h>
#include <sys/vfs.h>

/** VNode for VBoxVFS */
typedef struct vboxvfs_vnode
{
    vnode_t     *pVNode;
    vattr_t     Attr;
    SHFLSTRING  *pPath;
    kmutex_t    MtxContents;
} vboxvfs_vnode_t;


/** Per-file system mount instance data. */
typedef struct vboxvfs_globinfo
{
    VBGLSFMAP       Map;
    int             Ttl;
    int             Uid;
    int             Gid;
    vfs_t           *pVFS;
    vboxvfs_vnode_t *pVNodeRoot;
    kmutex_t        MtxFS;
} vboxvfs_globinfo_t;

extern struct vnodeops *g_pVBoxVFS_vnodeops;
extern const fs_operation_def_t g_VBoxVFS_vnodeops_template[];
extern VBGLSFCLIENT g_VBoxVFSClient;

/** Helper functions */
extern int vboxvfs_Stat(const char *pszCaller, vboxvfs_globinfo_t *pVBoxVFSGlobalInfo, SHFLSTRING *pPath,
                        PSHFLFSOBJINFO pResult, boolean_t fAllowFailure);
extern void vboxvfs_InitVNode(vboxvfs_globinfo_t *pVBoxVFSGlobalInfo, vboxvfs_vnode_t *pVBoxVNode,
                              PSHFLFSOBJINFO pFSInfo);


/** Helper macros */
#define VFS_TO_VBOXVFS(vfs)      ((vboxvfs_globinfo_t *)((vfs)->vfs_data))
#define VBOXVFS_TO_VFS(vboxvfs)  ((vboxvfs)->pVFS)
#define VN_TO_VBOXVN(vnode)      ((vboxvfs_vnode_t *)((vnode)->v_data))
#define VBOXVN_TO_VN(vboxvnode)  ((vboxvnode)->pVNode)

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* !GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_h */

