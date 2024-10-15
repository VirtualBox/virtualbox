/* $Id: vboxvfs_vnops.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/limits.h>
#include <sys/lockf.h>
#include <sys/stat.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

/*
 * Prototypes for VBOXVFS vnode operations
 */
static vop_create_t     vboxvfs_create;
static vop_mknod_t      vboxvfs_mknod;
static vop_open_t       vboxvfs_open;
static vop_close_t      vboxvfs_close;
static vop_access_t     vboxvfs_access;
static vop_getattr_t    vboxvfs_getattr;
static vop_setattr_t    vboxvfs_setattr;
static vop_read_t       vboxvfs_read;
static vop_write_t      vboxvfs_write;
static vop_fsync_t      vboxvfs_fsync;
static vop_remove_t     vboxvfs_remove;
static vop_link_t       vboxvfs_link;
static vop_lookup_t     vboxvfs_lookup;
static vop_rename_t     vboxvfs_rename;
static vop_mkdir_t      vboxvfs_mkdir;
static vop_rmdir_t      vboxvfs_rmdir;
static vop_symlink_t    vboxvfs_symlink;
static vop_readdir_t    vboxvfs_readdir;
static vop_strategy_t   vboxvfs_strategy;
static vop_print_t      vboxvfs_print;
static vop_pathconf_t   vboxvfs_pathconf;
static vop_advlock_t    vboxvfs_advlock;
static vop_getextattr_t vboxvfs_getextattr;
static vop_ioctl_t      vboxvfs_ioctl;
static vop_getpages_t   vboxvfs_getpages;
static vop_inactive_t   vboxvfs_inactive;
static vop_putpages_t   vboxvfs_putpages;
static vop_reclaim_t    vboxvfs_reclaim;

struct vop_vector vboxvfs_vnodeops = {
    .vop_default    =   &default_vnodeops,

    .vop_access     =   vboxvfs_access,
    .vop_advlock    =   vboxvfs_advlock,
    .vop_close      =   vboxvfs_close,
    .vop_create     =   vboxvfs_create,
    .vop_fsync      =   vboxvfs_fsync,
    .vop_getattr    =   vboxvfs_getattr,
    .vop_getextattr =   vboxvfs_getextattr,
    .vop_getpages   =   vboxvfs_getpages,
    .vop_inactive   =   vboxvfs_inactive,
    .vop_ioctl      =   vboxvfs_ioctl,
    .vop_link       =   vboxvfs_link,
    .vop_lookup     =   vboxvfs_lookup,
    .vop_mkdir      =   vboxvfs_mkdir,
    .vop_mknod      =   vboxvfs_mknod,
    .vop_open       =   vboxvfs_open,
    .vop_pathconf   =   vboxvfs_pathconf,
    .vop_print      =   vboxvfs_print,
    .vop_putpages   =   vboxvfs_putpages,
    .vop_read       =   vboxvfs_read,
    .vop_readdir    =   vboxvfs_readdir,
    .vop_reclaim    =   vboxvfs_reclaim,
    .vop_remove     =   vboxvfs_remove,
    .vop_rename     =   vboxvfs_rename,
    .vop_rmdir      =   vboxvfs_rmdir,
    .vop_setattr    =   vboxvfs_setattr,
    .vop_strategy   =   vboxvfs_strategy,
    .vop_symlink    =   vboxvfs_symlink,
    .vop_write      =   vboxvfs_write,
};

static int vboxvfs_access(struct vop_access_args *ap)
{
    return 0;
}

static int vboxvfs_open(struct vop_open_args *ap)
{
    return 0;
}

static int vboxvfs_close(struct vop_close_args *ap)
{
    return 0;
}

static int vboxvfs_getattr(struct vop_getattr_args *ap)
{
    return 0;
}

static int vboxvfs_setattr(struct vop_setattr_args *ap)
{
    return 0;
}

static int vboxvfs_read(struct vop_read_args *ap)
{
    return 0;
}

static int vboxvfs_write(struct vop_write_args *ap)
{
    return 0;
}

static int vboxvfs_create(struct vop_create_args *ap)
{
    return 0;
}

static int vboxvfs_remove(struct vop_remove_args *ap)
{
    return 0;
}

static int vboxvfs_rename(struct vop_rename_args *ap)
{
    return 0;
}

static int vboxvfs_link(struct vop_link_args *ap)
{
    return EOPNOTSUPP;
}

static int vboxvfs_symlink(struct vop_symlink_args *ap)
{
    return EOPNOTSUPP;
}

static int vboxvfs_mknod(struct vop_mknod_args *ap)
{
    return EOPNOTSUPP;
}

static int vboxvfs_mkdir(struct vop_mkdir_args *ap)
{
    return 0;
}

static int vboxvfs_rmdir(struct vop_rmdir_args *ap)
{
    return 0;
}

static int vboxvfs_readdir(struct vop_readdir_args *ap)
{
    return 0;
}

static int vboxvfs_fsync(struct vop_fsync_args *ap)
{
    return 0;
}

static int vboxvfs_print (struct vop_print_args *ap)
{
    return 0;
}

static int vboxvfs_pathconf (struct vop_pathconf_args *ap)
{
    return 0;
}

static int vboxvfs_strategy (struct vop_strategy_args *ap)
{
    return 0;
}

static int vboxvfs_ioctl(struct vop_ioctl_args *ap)
{
    return ENOTTY;
}

static int vboxvfs_getextattr(struct vop_getextattr_args *ap)
{
    return 0;
}

static int vboxvfs_advlock(struct vop_advlock_args *ap)
{
    return 0;
}

static int vboxvfs_lookup(struct vop_lookup_args *ap)
{
    return 0;
}

static int vboxvfs_inactive(struct vop_inactive_args *ap)
{
    return 0;
}

static int vboxvfs_reclaim(struct vop_reclaim_args *ap)
{
    return 0;
}

static int vboxvfs_getpages(struct vop_getpages_args *ap)
{
    return 0;
}

static int vboxvfs_putpages(struct vop_putpages_args *ap)
{
    return 0;
}

