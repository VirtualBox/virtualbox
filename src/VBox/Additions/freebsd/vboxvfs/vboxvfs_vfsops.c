/* $Id: vboxvfs_vfsops.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Description.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#include "vboxvfs.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <iprt/mem.h>

#define VFSMP2SFGLOBINFO(mp) ((struct sf_glob_info *)mp->mnt_data)

static int vboxvfs_version = VBOXVFS_VERSION;

SYSCTL_NODE(_vfs, OID_AUTO, vboxvfs, CTLFLAG_RW, 0, "VirtualBox shared filesystem");
SYSCTL_INT(_vfs_vboxvfs, OID_AUTO, version, CTLFLAG_RD, &vboxvfs_version, 0, "");

/* global connection to the host service. */
static VBGLSFCLIENT g_vboxSFClient;

static vfs_init_t       vboxvfs_init;
static vfs_uninit_t     vboxvfs_uninit;
static vfs_cmount_t     vboxvfs_cmount;
static vfs_mount_t      vboxvfs_mount;
static vfs_root_t       vboxvfs_root;
static vfs_quotactl_t   vboxvfs_quotactl;
static vfs_statfs_t     vboxvfs_statfs;
static vfs_unmount_t    vboxvfs_unmount;

static struct vfsops vboxvfs_vfsops = {
    .vfs_init     =    vboxvfs_init,
    .vfs_cmount   =    vboxvfs_cmount,
    .vfs_mount    =    vboxvfs_mount,
    .vfs_quotactl =    vboxvfs_quotactl,
    .vfs_root     =    vboxvfs_root,
    .vfs_statfs   =    vboxvfs_statfs,
    .vfs_sync     =    vfs_stdsync,
    .vfs_uninit   =    vboxvfs_uninit,
    .vfs_unmount  =    vboxvfs_unmount,
};


VFS_SET(vboxvfs_vfsops, vboxvfs, VFCF_NETWORK);
MODULE_DEPEND(vboxvfs, vboxguest, 1, 1, 1);

static int vboxvfs_cmount(struct mntarg *ma, void * data, int flags, struct thread *td)
{
    struct vboxvfs_mount_info args;
    int rc = 0;

    printf("%s: Enter\n", __FUNCTION__);

    rc = copyin(data, &args, sizeof(struct vboxvfs_mount_info));
    if (rc)
        return rc;

    ma = mount_argf(ma, "uid", "%d", args.uid);
    ma = mount_argf(ma, "gid", "%d", args.gid);
    ma = mount_arg(ma, "from", args.name, -1);

    rc = kernel_mount(ma, flags);

    printf("%s: Leave rc=%d\n", __FUNCTION__, rc);

    return rc;
}

static const char *vboxvfs_opts[] = {
    "uid", "gid", "from", "fstype", "fspath", "errmsg", NULL
};

static int vboxvfs_mount(struct mount *mp, struct thread *td)
{
    int rc;
    char *pszShare;
    int  cbShare, cbOption;
    int uid = 0, gid = 0;
    struct sf_glob_info *pShFlGlobalInfo;
    SHFLSTRING *pShFlShareName = NULL;
    int cbShFlShareName;

    printf("%s: Enter\n", __FUNCTION__);

    if (mp->mnt_flag & (MNT_UPDATE | MNT_ROOTFS))
        return EOPNOTSUPP;

    if (vfs_filteropt(mp->mnt_optnew, vboxvfs_opts))
    {
        vfs_mount_error(mp, "%s", "Invalid option");
        return EINVAL;
    }

    rc = vfs_getopt(mp->mnt_optnew, "from", (void **)&pszShare, &cbShare);
    if (rc || pszShare[cbShare-1] != '\0' || cbShare > 0xfffe)
        return EINVAL;

    rc = vfs_getopt(mp->mnt_optnew, "gid", (void **)&gid, &cbOption);
    if ((rc != ENOENT) && (rc || cbOption != sizeof(gid)))
        return EINVAL;

    rc = vfs_getopt(mp->mnt_optnew, "uid", (void **)&uid, &cbOption);
    if ((rc != ENOENT) && (rc || cbOption != sizeof(uid)))
        return EINVAL;

    pShFlGlobalInfo = RTMemAllocZ(sizeof(struct sf_glob_info));
    if (!pShFlGlobalInfo)
        return ENOMEM;

    cbShFlShareName = offsetof (SHFLSTRING, String.utf8) + cbShare + 1;
    pShFlShareName  = RTMemAllocZ(cbShFlShareName);
    if (!pShFlShareName)
        return VERR_NO_MEMORY;

    pShFlShareName->u16Length = cbShare;
    pShFlShareName->u16Size   = cbShare + 1;
    memcpy (pShFlShareName->String.utf8, pszShare, cbShare + 1);

    rc = VbglR0SfMapFolder (&g_vboxSFClient, pShFlShareName, &pShFlGlobalInfo->map);
    RTMemFree(pShFlShareName);

    if (RT_FAILURE (rc))
    {
        RTMemFree(pShFlGlobalInfo);
        printf("VbglR0SfMapFolder failed rc=%d\n", rc);
        return EPROTO;
    }

    pShFlGlobalInfo->uid = uid;
    pShFlGlobalInfo->gid = gid;

    mp->mnt_data = pShFlGlobalInfo;

    /** @todo root vnode. */

    vfs_getnewfsid(mp);
    vfs_mountedfrom(mp, pszShare);

    printf("%s: Leave rc=0\n", __FUNCTION__);

    return 0;
}

static int vboxvfs_unmount(struct mount *mp, int mntflags, struct thread *td)
{
    struct sf_glob_info *pShFlGlobalInfo = VFSMP2SFGLOBINFO(mp);
    int rc;
    int flags = 0;

    rc = VbglR0SfUnmapFolder(&g_vboxSFClient, &pShFlGlobalInfo->map);
    if (RT_FAILURE(rc))
        printf("Failed to unmap shared folder\n");

    if (mntflags & MNT_FORCE)
        flags |= FORCECLOSE;

    /* There is 1 extra root vnode reference (vnode_root). */
    rc = vflush(mp, 1, flags, td);
    if (rc)
        return rc;


    RTMemFree(pShFlGlobalInfo);
    mp->mnt_data = NULL;

    return 0;
}

static int vboxvfs_root(struct mount *mp, int flags, struct vnode **vpp, struct thread *td)
{
    int rc = 0;
    struct sf_glob_info *pShFlGlobalInfo = VFSMP2SFGLOBINFO(mp);
    struct vnode *vp;

    printf("%s: Enter\n", __FUNCTION__);

    vp = pShFlGlobalInfo->vnode_root;
    VREF(vp);

    vn_lock(vp, flags | LK_RETRY, td);
    *vpp = vp;

    printf("%s: Leave\n", __FUNCTION__);

    return rc;
}

static int vboxvfs_quotactl(struct mount *mp, int cmd, uid_t uid, void *arg, struct thread *td)
{
    return EOPNOTSUPP;
}

int vboxvfs_init(struct vfsconf *vfsp)
{
    int rc;

    /* Initialize the R0 guest library. */
    rc = VbglR0SfInit();
    if (RT_FAILURE(rc))
        return ENXIO;

    /* Connect to the host service. */
    rc = VbglR0SfConnect(&g_vboxSFClient);
    if (RT_FAILURE(rc))
    {
        printf("Failed to get connection to host! rc=%d\n", rc);
        VbglR0SfTerm();
        return ENXIO;
    }

    rc = VbglR0SfSetUtf8(&g_vboxSFClient);
    if (RT_FAILURE (rc))
    {
        printf("VbglR0SfSetUtf8 failed, rc=%d\n", rc);
        VbglR0SfDisconnect(&g_vboxSFClient);
        VbglR0SfTerm();
        return EPROTO;
    }

    printf("Successfully loaded shared folder module\n");

    return 0;
}

int vboxvfs_uninit(struct vfsconf *vfsp)
{
    VbglR0SfDisconnect(&g_vboxSFClient);
    VbglR0SfTerm();

    return 0;
}

int vboxvfs_statfs(struct mount *mp, struct statfs *sbp, struct thread *td)
{
    return 0;
}
