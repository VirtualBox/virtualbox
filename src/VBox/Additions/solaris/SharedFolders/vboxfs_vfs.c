/* $Id: vboxfs_vfs.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox File System for Solaris Guests, VFS implementation.
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

#include <VBox/log.h>
#include <VBox/version.h>
#include <iprt/dbg.h>

#include <sys/types.h>
#include <sys/mntent.h>
#include <sys/param.h>
#include <sys/modctl.h>
#include <sys/mount.h>
#include <sys/policy.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/vfs.h>
#if !defined(VBOX_VFS_SOLARIS_10U6)
# include <sys/vfs_opreg.h>
#endif
#include <sys/pathname.h>
#include <sys/cmn_err.h>
#include <sys/vmsystm.h>
#undef u /* /usr/include/sys/user.h:249:1 is where this is defined to (curproc->p_user). very cool. */

#include "vboxfs_prov.h"
#include "vboxfs_vnode.h"
#include "vboxfs_vfs.h"
#include "vboxfs.h"


#define VBOXSOLQUOTE2(x)                #x
#define VBOXSOLQUOTE(x)                 VBOXSOLQUOTE2(x)
/** The module name. */
#define DEVICE_NAME                     "vboxfs"
/** The module description as seen in 'modinfo'. */
#define DEVICE_DESC                     "VirtualBox ShrdFS"


/*
 * Shared Folders filesystem implementation of the Solaris VFS interfaces.
 * Much of this is cookie cutter code for Solaris filesystem implementation.
 */

/* forward declarations */
static int sffs_init(int fstype, char *name);
static int sffs_mount(vfs_t *, vnode_t *, struct mounta *, cred_t *);
static int sffs_unmount(vfs_t *vfsp, int flag, cred_t *cr);
static int sffs_root(vfs_t *vfsp, vnode_t **vpp);
static int sffs_statvfs(vfs_t *vfsp, statvfs64_t *sbp);

static mntopt_t sffs_options[] = {
	/* Option	Cancels Opt	Arg	Flags		Data */
	{"uid",		NULL,		NULL,	MO_HASVALUE,	NULL},
	{"gid",		NULL,		NULL,	MO_HASVALUE,	NULL},
	{"dmode",	NULL,		NULL,	MO_HASVALUE,	NULL},
	{"fmode",	NULL,		NULL,	MO_HASVALUE,	NULL},
	{"dmask",	NULL,		NULL,	MO_HASVALUE,	NULL},
	{"fmask",	NULL,		NULL,	MO_HASVALUE,	NULL},
	{"stat_ttl",	NULL,		NULL,	MO_HASVALUE,	NULL},
	{"fsync",	NULL,		NULL,	0,	        NULL},
	{"tag", 	NULL,		NULL,	MO_HASVALUE,	NULL}
};

static mntopts_t sffs_options_table = {
	sizeof (sffs_options) / sizeof (mntopt_t),
	sffs_options
};

static vfsdef_t sffs_vfsdef = {
	VFSDEF_VERSION,
	DEVICE_NAME,
	sffs_init,
	VSW_HASPROTO,
	&sffs_options_table
};

static int sffs_fstype;
static int sffs_major;	/* major number for device */

kmutex_t sffs_minor_lock;
int sffs_minor;		/* minor number for device */

/** Whether to use the old-style map_addr()/choose_addr() routines. */
bool                     g_fVBoxVFS_SolOldAddrMap;
/** The map_addr()/choose_addr() hooks callout table structure. */
VBoxVFS_SolAddrMap       g_VBoxVFS_SolAddrMap;

/*
 * Module linkage information
 */
#if defined(VBOX_VFS_SOLARIS_10U6)
struct modlfs_s10 {
    struct mod_ops   *fs_modops;
    char             *fs_linkinfo;
    struct vfsdef_v3 *fs_vfsdef;
};
static struct modlfs_s10 modlfs = {
#else
static struct modlfs modlfs = {
#endif
	&mod_fsops,
	DEVICE_DESC " " VBOX_VERSION_STRING "r" VBOXSOLQUOTE(VBOX_SVN_REV),
	&sffs_vfsdef
};

static struct modlinkage modlinkage = {
	MODREV_1, &modlfs, NULL
};

static sfp_connection_t *sfprov = NULL;

int
_init()
{
    RTDBGKRNLINFO hKrnlDbgInfo;
    int rc = RTR0DbgKrnlInfoOpen(&hKrnlDbgInfo, 0 /* fFlags */);
    if (RT_SUCCESS(rc))
    {
        rc = RTR0DbgKrnlInfoQuerySymbol(hKrnlDbgInfo, NULL /* pszModule */, "plat_map_align_amount",  NULL /* ppvSymbol */);
        if (RT_SUCCESS(rc))
        {
#if defined(VBOX_VFS_SOLARIS_10U6)
            g_VBoxVFS_SolAddrMap.MapAddr.pfnSol_map_addr    = (void *)map_addr;
#else
            g_VBoxVFS_SolAddrMap.ChooseAddr.pfnSol_choose_addr = (void *)choose_addr;
#endif
        }
        else
        {
            g_fVBoxVFS_SolOldAddrMap = true;
#if defined(VBOX_VFS_SOLARIS_10U6)
            g_VBoxVFS_SolAddrMap.MapAddr.pfnSol_map_addr_old    = (void *)map_addr;
#else
            g_VBoxVFS_SolAddrMap.ChooseAddr.pfnSol_choose_addr_old = (void *)choose_addr;
#endif
        }

        RTR0DbgKrnlInfoRelease(hKrnlDbgInfo);
    }
    else
    {
        cmn_err(CE_NOTE, "RTR0DbgKrnlInfoOpen failed. rc=%d\n", rc);
        return rc;
    }

	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


int
_fini()
{
	int error;

	error = mod_remove(&modlinkage);
	if (error)
		return (error);

	/*
	 * Tear down the operations vectors
	 */
	sffs_vnode_fini();
	(void) vfs_freevfsops_by_type(sffs_fstype);

	/*
	 * close connection to the provider
	 */
	sfprov_disconnect(sfprov);
	return (0);
}


static int
sffs_init(int fstype, char *name)
{
	static const fs_operation_def_t sffs_vfsops_template[] = {
		VFSNAME_MOUNT,		{ .vfs_mount = sffs_mount },
		VFSNAME_UNMOUNT,	{ .vfs_unmount = sffs_unmount },
		VFSNAME_ROOT,		{ .vfs_root = sffs_root },
		VFSNAME_STATVFS,	{ .vfs_statvfs = sffs_statvfs },
		NULL,			NULL
	};
	int error;

	ASSERT(fstype != 0);
	sffs_fstype = fstype;
	LogFlowFunc(("sffs_init() name=%s\n", name));

	/*
	 * This may seem a silly way to do things for now. But the code
	 * is structured to easily allow it to be used on other hypervisors
	 * which would have a different implementation of the provider.
	 * Hopefully that'll never happen. :)
	 */
	sfprov = sfprov_connect(SFPROV_VERSION);
	if (sfprov == NULL) {
		cmn_err(CE_WARN, "sffs_init: couldn't init sffs provider");
		return (ENODEV);
	}

	error = sfprov_set_show_symlinks();
	if (error != 0) {
		cmn_err(CE_WARN,  "sffs_init: host unable to show symlinks, "
						  "rc=%d\n", error);
	}

	error = vfs_setfsops(fstype, sffs_vfsops_template, NULL);
	if (error != 0) {
		cmn_err(CE_WARN, "sffs_init: bad vfs ops template");
		return (error);
	}

	error = sffs_vnode_init();
	if (error != 0) {
		(void) vfs_freevfsops_by_type(fstype);
		cmn_err(CE_WARN, "sffs_init: bad vnode ops template");
		return (error);
	}

	if ((sffs_major = getudev()) == (major_t)-1) {
		cmn_err(CE_WARN, "sffs_init: Can't get unique device number.");
		sffs_major = 0;
	}
	mutex_init(&sffs_minor_lock, NULL, MUTEX_DEFAULT, NULL);
	return (0);
}

/*
 * wrapper for pn_get
 */
static int
sf_pn_get(char *rawpath, struct mounta *uap, char **outpath)
{
	pathname_t path;
	int error;

	error = pn_get(rawpath, (uap->flags & MS_SYSSPACE) ? UIO_SYSSPACE :
	    UIO_USERSPACE, &path);
	if (error) {
		LogFlowFunc(("pn_get(%s) failed\n", rawpath));
		return (error);
	}
	*outpath = kmem_alloc(path.pn_pathlen + 1, KM_SLEEP);
	strcpy(*outpath, path.pn_path);
	pn_free(&path);
	return (0);
}

#ifdef DEBUG_ramshankar
static void
sffs_print(sffs_data_t *sffs)
{
	cmn_err(CE_NOTE, "sffs_data_t at 0x%p\n", sffs);
	cmn_err(CE_NOTE, "    vfs_t *sf_vfsp = 0x%p\n", sffs->sf_vfsp);
	cmn_err(CE_NOTE, "    vnode_t *sf_rootnode = 0x%p\n", sffs->sf_rootnode);
	cmn_err(CE_NOTE, "    uid_t sf_uid = 0x%lu\n", (ulong_t)sffs->sf_handle->sf_uid);
	cmn_err(CE_NOTE, "    gid_t sf_gid = 0x%lu\n", (ulong_t)sffs->sf_handle->sf_gid);
	cmn_err(CE_NOTE, "    mode_t sf_dmode = 0x%lu\n", (ulong_t)sffs->sf_handle->sf_dmode);
	cmn_err(CE_NOTE, "    mode_t sf_fmode = 0x%lu\n", (ulong_t)sffs->sf_handle->sf_fmode);
	cmn_err(CE_NOTE, "    mode_t sf_dmask = 0x%lu\n", (ulong_t)sffs->sf_handle->sf_dmask);
	cmn_err(CE_NOTE, "    mode_t sf_fmask = 0x%lu\n", (ulong_t)sffs->sf_handle->sf_fmask);
	cmn_err(CE_NOTE, "    char *sf_share_name = %s\n", sffs->sf_share_name);
	cmn_err(CE_NOTE, "    char *sf_mntpath = %s\n", sffs->sf_mntpath);
	cmn_err(CE_NOTE, "    sfp_mount_t *sf_handle = 0x%p\n", sffs->sf_handle);
}
#endif

static int
sffs_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
	sffs_data_t *sffs;
	char *mount_point = NULL;
	char *share_name = NULL;
	int error;
	dev_t dev;
	uid_t uid = 0;
	gid_t gid = 0;
	mode_t dmode = ~0U;
	mode_t fmode = ~0U;
	mode_t dmask = 0;
	mode_t fmask = 0;
	int stat_ttl = DEF_STAT_TTL_MS;
	int fsync = 0;
	char *optval;
	long val;
	char *path;
	sfp_mount_t *handle;
	sfnode_t *sfnode;

	/*
	 * check we have permission to do the mount
	 */
	LogFlowFunc(("sffs_mount() started\n"));
	error = secpolicy_fs_mount(cr, mvp, vfsp);
	if (error != 0)
		return (error);

	/*
	 * Mount point must be a directory
	 */
	if (mvp->v_type != VDIR)
		return (ENOTDIR);

	/*
	 * no support for remount (what is it?)
	 */
	if (uap->flags & MS_REMOUNT)
		return (ENOTSUP);

	/*
	 * Ensure that nothing else is actively in/under the mount point
	 */
	mutex_enter(&mvp->v_lock);
	if ((uap->flags & MS_OVERLAY) == 0 &&
	    (mvp->v_count != 1 || (mvp->v_flag & VROOT))) {
		mutex_exit(&mvp->v_lock);
		return (EBUSY);
	}
	mutex_exit(&mvp->v_lock);

	/*
	 * check for read only has to be done early
	 */
	if (uap->flags & MS_RDONLY) {
		vfsp->vfs_flag |= VFS_RDONLY;
		vfs_setmntopt(vfsp, MNTOPT_RO, NULL, 0);
	}

	/*
	 * UID to use for all files
	 */
	if (vfs_optionisset(vfsp, "uid", &optval) &&
	    ddi_strtol(optval, NULL, 10, &val) == 0 &&
	    (uid_t)val == val)
		uid = val;

	/*
	 * GID to use for all files
	 */
	if (vfs_optionisset(vfsp, "gid", &optval) &&
	    ddi_strtol(optval, NULL, 10, &val) == 0 &&
	    (gid_t)val == val)
		gid = val;

	/*
	 * dmode to use for all directories
	 */
	if (vfs_optionisset(vfsp, "dmode", &optval) &&
		ddi_strtol(optval, NULL, 8, &val) == 0 &&
		(mode_t)val == val)
		dmode = val;

	/*
	 * fmode to use for all files
	 */
	if (vfs_optionisset(vfsp, "fmode", &optval) &&
		ddi_strtol(optval, NULL, 8, &val) == 0 &&
		(mode_t)val == val)
		fmode = val;

	/*
	 * dmask to use for all directories
	 */
	if (vfs_optionisset(vfsp, "dmask", &optval) &&
		ddi_strtol(optval, NULL, 8, &val) == 0 &&
		(mode_t)val == val)
		dmask = val;

	/*
	 * fmask to use for all files
	 */
	if (vfs_optionisset(vfsp, "fmask", &optval) &&
		ddi_strtol(optval, NULL, 8, &val) == 0 &&
		(mode_t)val == val)
		fmask = val;

	/*
	 * umask to use for all directories & files
	 */
	if (vfs_optionisset(vfsp, "umask", &optval) &&
		ddi_strtol(optval, NULL, 8, &val) == 0 &&
		(mode_t)val == val)
		dmask = fmask = val;

	/*
	 * ttl to use for stat caches
	 */
	if (vfs_optionisset(vfsp, "stat_ttl", &optval) &&
	    ddi_strtol(optval, NULL, 10, &val) == 0 &&
	    (int)val == val)
	{
		stat_ttl = val;
	}
	else
		vfs_setmntopt(vfsp, "stat_ttl", VBOXSOLQUOTE(DEF_STAT_TTL_MS), 0);

	/*
	 * whether to honor fsync
	 */
	if (vfs_optionisset(vfsp, "fsync", &optval))
		fsync = 1;

	/*
	 * Any unknown options are an error
	 */
	if ((uap->flags & MS_DATA) && uap->datalen > 0) {
		cmn_err(CE_WARN, "sffs: unknown mount options specified");
		return (EINVAL);
	}

	/*
	 * get the mount point pathname
	 */
	error = sf_pn_get(uap->dir, uap, &mount_point);
	if (error)
		return (error);

	/*
	 * find what we are mounting
	 */
	error = sf_pn_get(uap->spec, uap, &share_name);
	if (error) {
		kmem_free(mount_point, strlen(mount_point) + 1);
		return (error);
	}

	/*
	 * Invoke Hypervisor mount interface before proceeding
	 */
	error = sfprov_mount(sfprov, share_name, &handle);
	if (error) {
		kmem_free(share_name, strlen(share_name) + 1);
		kmem_free(mount_point, strlen(mount_point) + 1);
		return (error);
	}

	/*
	 * find an available minor device number for this mount
	 */
	mutex_enter(&sffs_minor_lock);
	do {
		sffs_minor = (sffs_minor + 1) & L_MAXMIN32;
		dev = makedevice(sffs_major, sffs_minor);
	} while (vfs_devismounted(dev));
	mutex_exit(&sffs_minor_lock);

	/*
	 * allocate and fill in the sffs structure
	 */
	sffs = kmem_alloc(sizeof (*sffs), KM_SLEEP);
	sffs->sf_vfsp = vfsp;
	sffs->sf_handle = handle;
	sffs->sf_handle->sf_uid = uid;
	sffs->sf_handle->sf_gid = gid;
	sffs->sf_handle->sf_dmode = dmode;
	sffs->sf_handle->sf_fmode = fmode;
	sffs->sf_handle->sf_dmask = dmask;
	sffs->sf_handle->sf_fmask = fmask;
	sffs->sf_stat_ttl = stat_ttl;
	sffs->sf_fsync = fsync;
	sffs->sf_share_name = share_name;
	sffs->sf_mntpath = mount_point;
	sffs->sf_ino = 3;	/* root mount point is always '3' */

	/*
	 * fill in the vfs structure
	 */
	vfsp->vfs_data = (caddr_t)sffs;
	vfsp->vfs_fstype = sffs_fstype;
	vfsp->vfs_dev = dev;
	vfsp->vfs_bsize = PAGESIZE; /* HERE JOE ??? */
	vfsp->vfs_flag |= VFS_NOTRUNC; /* HERE JOE ???? */
	vfs_make_fsid(&vfsp->vfs_fsid, dev, sffs_fstype);

	/*
	 * create the root vnode.
	 * XXX JOE What should the path be here? is "/" really right?
	 * other options?
	 */
	path = kmem_alloc(2, KM_SLEEP);
	strcpy(path, ".");
	mutex_enter(&sffs_lock);
	sfnode = sfnode_make(sffs, path, VDIR, NULL, NULL, NULL, 0);
	sffs->sf_rootnode = sfnode_get_vnode(sfnode);
	sffs->sf_rootnode->v_flag |= VROOT;
	sffs->sf_rootnode->v_vfsp = vfsp;
	mutex_exit(&sffs_lock);

	LogFlowFunc(("sffs_mount() success sffs=0x%p\n", sffs));
#ifdef DEBUG_ramshankar
	sffs_print(sffs);
#endif
	return (error);
}

static int
sffs_unmount(vfs_t *vfsp, int flag, cred_t *cr)
{
	sffs_data_t *sffs = (sffs_data_t *)vfsp->vfs_data;
	int error;

	/*
	 * generic security check
	 */
	LogFlowFunc(("sffs_unmount() of sffs=0x%p\n", sffs));
	if ((error = secpolicy_fs_unmount(cr, vfsp)) != 0)
		return (error);

	/*
	 * forced unmount is not supported by this file system
	 * and thus, ENOTSUP, is being returned.
	 */
	if (flag & MS_FORCE) {
		LogFlowFunc(("sffs_unmount(MS_FORCE) returns ENOSUP\n"));
		return (ENOTSUP);
	}

	/*
	 * Mark the file system unmounted.
	 */
	vfsp->vfs_flag |= VFS_UNMOUNTED;

	/*
	 * Make sure nothing is still in use.
	 */
	if (sffs_purge(sffs) != 0) {
		vfsp->vfs_flag &= ~VFS_UNMOUNTED;
		LogFlowFunc(("sffs_unmount() returns EBUSY\n"));
		return (EBUSY);
	}

	/*
	 * Invoke Hypervisor unmount interface before proceeding
	 */
	error = sfprov_unmount(sffs->sf_handle);
	if (error != 0) {
		/* TBD anything here? */
	}

	kmem_free(sffs->sf_share_name, strlen(sffs->sf_share_name) + 1);
	kmem_free(sffs->sf_mntpath, strlen(sffs->sf_mntpath) + 1);
	kmem_free(sffs, sizeof(*sffs));
	LogFlowFunc(("sffs_unmount() done\n"));
	return (0);
}

/*
 * return the vnode for the root of the mounted file system
 */
static int
sffs_root(vfs_t *vfsp, vnode_t **vpp)
{
	sffs_data_t *sffs = (sffs_data_t *)vfsp->vfs_data;
	vnode_t *vp = sffs->sf_rootnode;

	VN_HOLD(vp);
	*vpp = vp;
	return (0);
}

/*
 * get some stats.. fake up the rest
 */
static int
sffs_statvfs(vfs_t *vfsp, statvfs64_t *sbp)
{
	sffs_data_t *sffs = (sffs_data_t *)vfsp->vfs_data;
	sffs_fsinfo_t fsinfo;
	dev32_t d32;
	int error;

	bzero(sbp, sizeof(*sbp));
	error = sfprov_get_fsinfo(sffs->sf_handle, &fsinfo);
	if (error != 0)
		return (error);

	sbp->f_bsize = fsinfo.blksize;
	sbp->f_frsize = fsinfo.blksize;

	sbp->f_bfree = fsinfo.blksavail;
	sbp->f_bavail = fsinfo.blksavail;
	sbp->f_files = fsinfo.blksavail / 4; /* some kind of reasonable value */
	sbp->f_ffree = fsinfo.blksavail / 4;
	sbp->f_favail = fsinfo.blksavail / 4;

	sbp->f_blocks = fsinfo.blksused + sbp->f_bavail;

	(void) cmpldev(&d32, vfsp->vfs_dev);
	sbp->f_fsid = d32;
	strcpy(&sbp->f_basetype[0], "sffs");
	sbp->f_flag |= ST_NOSUID;

	if (fsinfo.readonly)
		sbp->f_flag |= ST_RDONLY;

	sbp->f_namemax = fsinfo.maxnamesize;
	return (0);
}

