/* $Id: vboxfs_prov.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox File System for Solaris Guests, provider implementation.
 * Portions contributed by: Ronald.
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

/*
 * Provider interfaces for shared folder file system.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mntent.h>
#include <sys/param.h>
#include <sys/modctl.h>
#include <sys/mount.h>
#include <sys/policy.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/dirent.h>
#include <sys/file.h>
#undef u /* /usr/include/sys/user.h:249:1 is where this is defined to (curproc->p_user). very cool. */

#include "vboxfs_prov.h"
#include <iprt/err.h>

#define	SFPROV_VERSION	1

static VBGLSFCLIENT vbox_client;

static int sfprov_vbox2errno(int rc)
{
	if (rc == VERR_ACCESS_DENIED)
		return (EACCES);
	if (rc == VERR_INVALID_NAME)
	    return (ENOENT);
	return (RTErrConvertToErrno(rc));
}

/*
 * utility to create strings
 */
static SHFLSTRING *
sfprov_string(char *path, int *sz)
{
	SHFLSTRING *str;
	int len = strlen(path);

	*sz = len + 1 + sizeof (*str) - sizeof (str->String);
	str = kmem_zalloc(*sz, KM_SLEEP);
	str->u16Size = len + 1;
	str->u16Length = len;
	strcpy(str->String.utf8, path);
	return (str);
}

sfp_connection_t *
sfprov_connect(int version)
{
	/*
	 * only one version for now, so must match
	 */
	int rc = -1;
	if (version != SFPROV_VERSION)
	{
		cmn_err(CE_WARN, "sfprov_connect: wrong version. version=%d expected=%d\n", version, SFPROV_VERSION);
		return NULL;
	}
	rc = VbglR0SfInit();
	if (RT_SUCCESS(rc))
	{
		rc = VbglR0SfConnect(&vbox_client);
		if (RT_SUCCESS(rc))
		{
			rc = VbglR0SfSetUtf8(&vbox_client);
			if (RT_SUCCESS(rc))
			{
				return ((sfp_connection_t *)&vbox_client);
			}
			else
				cmn_err(CE_WARN, "sfprov_connect: VbglR0SfSetUtf8() failed\n");

			VbglR0SfDisconnect(&vbox_client);
		}
		else
			cmn_err(CE_WARN, "sfprov_connect: VbglR0SfConnect() failed rc=%d\n", rc);
		VbglR0SfTerm();
	}
	else
		cmn_err(CE_WARN, "sfprov_connect: VbglR0SfInit() failed rc=%d\n", rc);
	return (NULL);
}

void
sfprov_disconnect(sfp_connection_t *conn)
{
	if (conn != (sfp_connection_t *)&vbox_client)
		cmn_err(CE_WARN, "sfprov_disconnect: bad argument\n");
	VbglR0SfDisconnect(&vbox_client);
	VbglR0SfTerm();
}


int
sfprov_mount(sfp_connection_t *conn, char *path, sfp_mount_t **mnt)
{
	sfp_mount_t *m;
	SHFLSTRING *str;
	int size;
	int rc;

	m = kmem_zalloc(sizeof (*m), KM_SLEEP);
	str = sfprov_string(path, &size);
	rc = VbglR0SfMapFolder(&vbox_client, str, &m->map);
	if (RT_FAILURE(rc)) {
		cmn_err(CE_WARN, "sfprov_mount: VbglR0SfMapFolder() failed. path=%s rc=%d\n", path, rc);
		kmem_free(m, sizeof (*m));
		*mnt = NULL;
		rc = EINVAL;
	} else {
		*mnt = m;
		rc = 0;
	}
	kmem_free(str, size);
	return (rc);
}

int
sfprov_unmount(sfp_mount_t *mnt)
{
	int rc;

	rc = VbglR0SfUnmapFolder(&vbox_client, &mnt->map);
	if (RT_FAILURE(rc)) {
		cmn_err(CE_WARN, "sfprov_mount: VbglR0SfUnmapFolder() failed rc=%d\n", rc);
		rc = EINVAL;
	} else {
		rc = 0;
	}
	kmem_free(mnt, sizeof (*mnt));
	return (rc);
}

/*
 * query information about a mounted file system
 */
int
sfprov_get_fsinfo(sfp_mount_t *mnt, sffs_fsinfo_t *fsinfo)
{
	int rc;
	SHFLVOLINFO info;
	uint32_t bytes = sizeof(SHFLVOLINFO);

	rc = VbglR0SfFsInfo(&vbox_client, &mnt->map, 0, SHFL_INFO_GET | SHFL_INFO_VOLUME,
			    &bytes, (SHFLDIRINFO *)&info);
	if (RT_FAILURE(rc))
		return (EINVAL);

	fsinfo->blksize = info.ulBytesPerAllocationUnit;
	fsinfo->blksused = (info.ullTotalAllocationBytes - info.ullAvailableAllocationBytes) / info.ulBytesPerAllocationUnit;
	fsinfo->blksavail = info.ullAvailableAllocationBytes / info.ulBytesPerAllocationUnit;
	fsinfo->maxnamesize = info.fsProperties.cbMaxComponent;
	fsinfo->readonly = info.fsProperties.fReadOnly;
	return (0);
}

/*
 * file/directory information conversions.
 */
static void
sfprov_fmode_from_mode(RTFMODE *fMode, mode_t mode)
{
	RTFMODE m = 0;

#define mode_set(r) ((mode) & (S_##r)) ? RTFS_UNIX_##r : 0
	m  = mode_set (ISUID);
	m |= mode_set (ISGID);
	m |= (mode & S_ISVTX) ? RTFS_UNIX_ISTXT : 0;

	m |= mode_set (IRUSR);
	m |= mode_set (IWUSR);
	m |= mode_set (IXUSR);

	m |= mode_set (IRGRP);
	m |= mode_set (IWGRP);
	m |= mode_set (IXGRP);

	m |= mode_set (IROTH);
	m |= mode_set (IWOTH);
	m |= mode_set (IXOTH);
#undef mode_set

	if (S_ISDIR(mode))
		m |= RTFS_TYPE_DIRECTORY;
	else if (S_ISREG(mode))
		m |= RTFS_TYPE_FILE;
	else if (S_ISFIFO(mode))
		m |= RTFS_TYPE_FIFO;
	else if (S_ISCHR(mode))
		m |= RTFS_TYPE_DEV_CHAR;
	else if (S_ISBLK(mode))
		m |= RTFS_TYPE_DEV_BLOCK;
	else if (S_ISLNK(mode))
		m |= RTFS_TYPE_SYMLINK;
	else if (S_ISSOCK(mode))
		m |= RTFS_TYPE_SOCKET;
	else
		m |= RTFS_TYPE_FILE;

	*fMode = m;
}

static void
sfprov_mode_from_fmode(sfp_mount_t *mnt, mode_t *mode, RTFMODE fMode)
{
	mode_t m = 0;

#define mode_set_from_rt(r) ((fMode) & (RTFS_UNIX_##r)) ? (S_##r) : 0;
	m  = mode_set_from_rt(ISUID);
	m |= mode_set_from_rt(ISGID);
	m |= (fMode & RTFS_UNIX_ISTXT) ? S_ISVTX : 0;

	m |= mode_set_from_rt(IRUSR);
	m |= mode_set_from_rt(IWUSR);
	m |= mode_set_from_rt(IXUSR);

	m |= mode_set_from_rt(IRGRP);
	m |= mode_set_from_rt(IWGRP);
	m |= mode_set_from_rt(IXGRP);

	m |= mode_set_from_rt(IROTH);
	m |= mode_set_from_rt(IWOTH);
	m |= mode_set_from_rt(IXOTH);
#undef mode_set_from_rt

	if (RTFS_IS_DIRECTORY(fMode))
	{
		m = mnt->sf_dmode != ~0U ? (mnt->sf_dmode & PERMMASK) : m;
		m &= ~mnt->sf_dmask;
		m |= S_IFDIR;
	}
	else
	{
		m = mnt->sf_fmode != ~0U ? (mnt->sf_fmode & PERMMASK) : m;
		m &= ~mnt->sf_fmask;

		if (RTFS_IS_FILE(fMode))
			m |= S_IFREG;
		else if (RTFS_IS_SYMLINK(fMode))
			m |= S_IFLNK;
		else if (RTFS_IS_FIFO(fMode))
			m |= S_IFIFO;
		else if (RTFS_IS_DEV_CHAR(fMode))
			m |= S_IFCHR;
		else if (RTFS_IS_DEV_BLOCK(fMode))
			m |= S_IFBLK;
		else if (RTFS_IS_SOCKET(fMode))
			m |= S_IFSOCK;
	}

	*mode = m;
}

static void
sfprov_ftime_from_timespec(timestruc_t *time, RTTIMESPEC *ts)
{
	uint64_t nanosec = RTTimeSpecGetNano(ts);
	time->tv_sec = nanosec / UINT64_C(1000000000);
	time->tv_nsec = nanosec % UINT64_C(1000000000);
}

static void
sfprov_stat_from_info(sfp_mount_t *mnt, sffs_stat_t *stat, SHFLFSOBJINFO *info)
{
	sfprov_mode_from_fmode(mnt, &stat->sf_mode, info->Attr.fMode);
	stat->sf_size  = info->cbObject;
	stat->sf_alloc = info->cbAllocated;
	sfprov_ftime_from_timespec(&stat->sf_atime, &info->AccessTime);
	sfprov_ftime_from_timespec(&stat->sf_mtime, &info->ModificationTime);
	sfprov_ftime_from_timespec(&stat->sf_ctime, &info->ChangeTime);
}

/*
 * File operations: open/close/read/write/etc.
 *
 * open/create can return any relevant errno, however ENOENT
 * generally means that the host file didn't exist.
 */
struct sfp_file {
	SHFLHANDLE handle;
	VBGLSFMAP map;	/**< need this again for the close operation */
};

int
sfprov_create(
	sfp_mount_t *mnt,
	char *path,
	mode_t mode,
	sfp_file_t **fp,
	sffs_stat_t *stat)
{

	int rc;
	SHFLCREATEPARMS parms;
	SHFLSTRING *str;
	int size;
	sfp_file_t *newfp;

	str = sfprov_string(path, &size);
	parms.Handle = SHFL_HANDLE_NIL;
	parms.Info.cbObject = 0;
	sfprov_fmode_from_mode(&parms.Info.Attr.fMode, mode);
	parms.CreateFlags = SHFL_CF_ACT_CREATE_IF_NEW |
	    SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACCESS_READWRITE;
	rc = VbglR0SfCreate(&vbox_client, &mnt->map, str, &parms);
	kmem_free(str, size);

	if (RT_FAILURE(rc))
	{
		if (rc != VERR_ACCESS_DENIED && rc != VERR_WRITE_PROTECT)
			cmn_err(CE_WARN, "sfprov_create: VbglR0SfCreate failed! path=%s rc=%d\n", path, rc);
		return (sfprov_vbox2errno(rc));
	}
	if (parms.Handle == SHFL_HANDLE_NIL) {
		if (parms.Result == SHFL_FILE_EXISTS)
			return (EEXIST);
		return (ENOENT);
	}
	newfp = kmem_alloc(sizeof(sfp_file_t), KM_SLEEP);
	newfp->handle = parms.Handle;
	newfp->map = mnt->map;
	*fp = newfp;
	sfprov_stat_from_info(mnt, stat, &parms.Info);
	return (0);
}

int
sfprov_diropen(sfp_mount_t *mnt, char *path, sfp_file_t **fp)
{
	int rc;
	SHFLCREATEPARMS parms;
	SHFLSTRING *str;
	int size;
	sfp_file_t *newfp;

	bzero(&parms, sizeof(parms));
	str = sfprov_string(path, &size);
	parms.Handle = SHFL_HANDLE_NIL;
	parms.Info.cbObject = 0;
	parms.CreateFlags =   SHFL_CF_DIRECTORY
						| SHFL_CF_ACCESS_READ
						| SHFL_CF_ACT_OPEN_IF_EXISTS
						| SHFL_CF_ACT_FAIL_IF_NEW;

	/*
	 * Open the host directory.
	 */
	rc = VbglR0SfCreate(&vbox_client, &mnt->map, str, &parms);

	/*
	 * Our VBoxFS interface here isn't very clear regarding failure and informational status.
	 * Check the file-handle as well as the return code to make sure the operation succeeded.
	 */
	if (RT_FAILURE(rc)) {
		kmem_free(str, size);
		return (sfprov_vbox2errno(rc));
	}

	if (parms.Handle == SHFL_HANDLE_NIL) {
		kmem_free(str, size);
		return (ENOENT);
	}

	newfp = kmem_alloc(sizeof(sfp_file_t), KM_SLEEP);
	newfp->handle = parms.Handle;
	newfp->map = mnt->map;
	*fp = newfp;
	return (0);
}

int
sfprov_open(sfp_mount_t *mnt, char *path, sfp_file_t **fp, int flag)
{
	int rc;
	SHFLCREATEPARMS parms;
	SHFLSTRING *str;
	int size;
	sfp_file_t *newfp;

	bzero(&parms, sizeof(parms));
	str = sfprov_string(path, &size);
	parms.Handle = SHFL_HANDLE_NIL;
	parms.Info.cbObject = 0;

	/*
	 * Translate file modes.
	 */
	if (flag & FCREAT) {
		parms.CreateFlags |= SHFL_CF_ACT_CREATE_IF_NEW;
		if (!(flag & FTRUNC))
			parms.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
	}
	else
		parms.CreateFlags |= SHFL_CF_ACT_FAIL_IF_NEW;

	if (flag & FTRUNC)
		parms.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACCESS_WRITE;
	if (flag & FWRITE)
		parms.CreateFlags |= SHFL_CF_ACCESS_WRITE;
	if (flag & FREAD)
		parms.CreateFlags |= SHFL_CF_ACCESS_READ;
    if (flag & FAPPEND)
		parms.CreateFlags |= SHFL_CF_ACCESS_APPEND;

	/*
	 * Open/create the host file.
	 */
	rc = VbglR0SfCreate(&vbox_client, &mnt->map, str, &parms);

	/*
	 * Our VBoxFS interface here isn't very clear regarding failure and informational status.
	 * Check the file-handle as well as the return code to make sure the operation succeeded.
	 */
	if (RT_FAILURE(rc)) {
		kmem_free(str, size);
		return (sfprov_vbox2errno(rc));
	}

	if (parms.Handle == SHFL_HANDLE_NIL) {
		kmem_free(str, size);
		return (ENOENT);
	}

	newfp = kmem_alloc(sizeof(sfp_file_t), KM_SLEEP);
	newfp->handle = parms.Handle;
	newfp->map = mnt->map;
	*fp = newfp;
	return (0);
}

int
sfprov_close(sfp_file_t *fp)
{
	int rc;

	rc = VbglR0SfClose(&vbox_client, &fp->map, fp->handle);
	kmem_free(fp, sizeof(sfp_file_t));
	return (0);
}

int
sfprov_read(sfp_file_t *fp, char *buffer, uint64_t offset, uint32_t *numbytes)
{
	int rc;

	rc = VbglR0SfRead(&vbox_client, &fp->map, fp->handle, offset,
			  numbytes, (uint8_t *)buffer, 0 /*fLocked*/);
	if (RT_FAILURE(rc))
		return (EINVAL);
	return (0);
}

int
sfprov_write(sfp_file_t *fp, char *buffer, uint64_t offset, uint32_t *numbytes)
{
	int rc;

	rc = VbglR0SfWrite(&vbox_client, &fp->map, fp->handle, offset,
			   numbytes, (uint8_t *)buffer, 0 /*fLocked*/);
	if (RT_FAILURE(rc))
		return (EINVAL);
	return (0);
}

int
sfprov_fsync(sfp_file_t *fp)
{
	int rc;

	rc = VbglR0SfFlush(&vbox_client, &fp->map, fp->handle);
	if (RT_FAILURE(rc))
		return (EIO);
	return (0);
}


static int
sfprov_getinfo(sfp_mount_t *mnt, char *path, PSHFLFSOBJINFO info)
{
	int rc;
	SHFLCREATEPARMS parms;
	SHFLSTRING *str;
	int size;

	str = sfprov_string(path, &size);
	parms.Handle = 0;
	parms.Info.cbObject = 0;
	parms.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;
	rc = VbglR0SfCreate(&vbox_client, &mnt->map, str, &parms);
	kmem_free(str, size);

	if (RT_FAILURE(rc))
		return (EINVAL);
	if (parms.Result != SHFL_FILE_EXISTS)
		return (ENOENT);
	*info = parms.Info;
	return (0);
}

/*
 * get information about a file (or directory)
 */
int
sfprov_get_mode(sfp_mount_t *mnt, char *path, mode_t *mode)
{
	int rc;
	SHFLFSOBJINFO info;

	rc = sfprov_getinfo(mnt, path, &info);
	if (rc)
		return (rc);
	sfprov_mode_from_fmode(mnt, mode, info.Attr.fMode);
	return (0);
}

int
sfprov_get_size(sfp_mount_t *mnt, char *path, uint64_t *size)
{
	int rc;
	SHFLFSOBJINFO info;

	rc = sfprov_getinfo(mnt, path, &info);
	if (rc)
		return (rc);
	*size = info.cbObject;
	return (0);
}


int
sfprov_get_atime(sfp_mount_t *mnt, char *path, timestruc_t *time)
{
	int rc;
	SHFLFSOBJINFO info;

	rc = sfprov_getinfo(mnt, path, &info);
	if (rc)
		return (rc);
	sfprov_ftime_from_timespec(time, &info.AccessTime);
	return (0);
}

int
sfprov_get_mtime(sfp_mount_t *mnt, char *path, timestruc_t *time)
{
	int rc;
	SHFLFSOBJINFO info;

	rc = sfprov_getinfo(mnt, path, &info);
	if (rc)
		return (rc);
	sfprov_ftime_from_timespec(time, &info.ModificationTime);
	return (0);
}

int
sfprov_get_ctime(sfp_mount_t *mnt, char *path, timestruc_t *time)
{
	int rc;
	SHFLFSOBJINFO info;

	rc = sfprov_getinfo(mnt, path, &info);
	if (rc)
		return (rc);
	sfprov_ftime_from_timespec(time, &info.ChangeTime);
	return (0);
}

int
sfprov_get_attr(sfp_mount_t *mnt, char *path, sffs_stat_t *attr)
{
	int rc;
	SHFLFSOBJINFO info;

	rc = sfprov_getinfo(mnt, path, &info);
	if (rc)
		return (rc);
	sfprov_stat_from_info(mnt, attr, &info);
	return (0);
}

static void
sfprov_timespec_from_ftime(RTTIMESPEC *ts, timestruc_t time)
{
	uint64_t nanosec = UINT64_C(1000000000) * time.tv_sec + time.tv_nsec;
	RTTimeSpecSetNano(ts, nanosec);
}

int
sfprov_set_attr(
	sfp_mount_t *mnt,
	char *path,
	uint_t mask,
	mode_t mode,
	timestruc_t atime,
	timestruc_t mtime,
	timestruc_t ctime)
{
	int rc, err;
	SHFLCREATEPARMS parms;
	SHFLSTRING *str;
	SHFLFSOBJINFO info;
	uint32_t bytes;
	int str_size;

	str = sfprov_string(path, &str_size);
	parms.Handle = 0;
	parms.Info.cbObject = 0;
	parms.CreateFlags = SHFL_CF_ACT_OPEN_IF_EXISTS
			  | SHFL_CF_ACT_FAIL_IF_NEW
			  | SHFL_CF_ACCESS_ATTR_WRITE;

	rc = VbglR0SfCreate(&vbox_client, &mnt->map, str, &parms);

	if (RT_FAILURE(rc)) {
		cmn_err(CE_WARN, "sfprov_set_attr: VbglR0SfCreate(%s) failed rc=%d\n",
		    path, rc);
		err = EINVAL;
		goto fail2;
	}
	if (parms.Result != SHFL_FILE_EXISTS) {
		err = ENOENT;
		goto fail1;
	}

	RT_ZERO(info);
	if (mask & AT_MODE)
		sfprov_fmode_from_mode(&info.Attr.fMode, mode);
	if (mask & AT_ATIME)
		sfprov_timespec_from_ftime(&info.AccessTime, atime);
	if (mask & AT_MTIME)
		sfprov_timespec_from_ftime(&info.ModificationTime, mtime);
	if (mask & AT_CTIME)
		sfprov_timespec_from_ftime(&info.ChangeTime, ctime);

	bytes = sizeof(info);
	rc = VbglR0SfFsInfo(&vbox_client, &mnt->map, parms.Handle, SHFL_INFO_SET | SHFL_INFO_FILE,
			    &bytes, (SHFLDIRINFO *)&info);
	if (RT_FAILURE(rc)) {
		if (rc != VERR_ACCESS_DENIED && rc != VERR_WRITE_PROTECT)
		{
			cmn_err(CE_WARN, "sfprov_set_attr: VbglR0SfFsInfo(%s, FILE) failed rc=%d\n",
		    path, rc);
		}
		err = sfprov_vbox2errno(rc);
		goto fail1;
	}

	err = 0;

fail1:
	rc = VbglR0SfClose(&vbox_client, &mnt->map, parms.Handle);
	if (RT_FAILURE(rc)) {
		cmn_err(CE_WARN, "sfprov_set_attr: VbglR0SfClose(%s) failed rc=%d\n",
		    path, rc);
	}
fail2:
	kmem_free(str, str_size);
	return err;
}

int
sfprov_set_size(sfp_mount_t *mnt, char *path, uint64_t size)
{
	int rc, err;
	SHFLCREATEPARMS parms;
	SHFLSTRING *str;
	SHFLFSOBJINFO info;
	uint32_t bytes;
	int str_size;

	str = sfprov_string(path, &str_size);
	parms.Handle = 0;
	parms.Info.cbObject = 0;
	parms.CreateFlags = SHFL_CF_ACT_OPEN_IF_EXISTS
			  | SHFL_CF_ACT_FAIL_IF_NEW
			  | SHFL_CF_ACCESS_WRITE;

	rc = VbglR0SfCreate(&vbox_client, &mnt->map, str, &parms);

	if (RT_FAILURE(rc)) {
		cmn_err(CE_WARN, "sfprov_set_size: VbglR0SfCreate(%s) failed rc=%d\n",
		    path, rc);
		err = EINVAL;
		goto fail2;
	}
	if (parms.Result != SHFL_FILE_EXISTS) {
		err = ENOENT;
		goto fail1;
	}

	RT_ZERO(info);
	info.cbObject = size;
	bytes = sizeof(info);
	rc = VbglR0SfFsInfo(&vbox_client, &mnt->map, parms.Handle, SHFL_INFO_SET | SHFL_INFO_SIZE,
			    &bytes, (SHFLDIRINFO *)&info);
	if (RT_FAILURE(rc)) {
		cmn_err(CE_WARN, "sfprov_set_size: VbglR0SfFsInfo(%s, SIZE) failed rc=%d\n",
		    path, rc);
		err = sfprov_vbox2errno(rc);
		goto fail1;
	}

	err = 0;

fail1:
	rc = VbglR0SfClose(&vbox_client, &mnt->map, parms.Handle);
	if (RT_FAILURE(rc)) {
		cmn_err(CE_WARN, "sfprov_set_size: VbglR0SfClose(%s) failed rc=%d\n",
		    path, rc);
	}
fail2:
	kmem_free(str, str_size);
	return err;
}

/*
 * Directory operations
 */
int
sfprov_mkdir(
	sfp_mount_t *mnt,
	char *path,
	mode_t mode,
	sfp_file_t **fp,
	sffs_stat_t *stat)
{
	int rc;
	SHFLCREATEPARMS parms;
	SHFLSTRING *str;
	int size;
	sfp_file_t *newfp;

	str = sfprov_string(path, &size);
	parms.Handle = SHFL_HANDLE_NIL;
	parms.Info.cbObject = 0;
	sfprov_fmode_from_mode(&parms.Info.Attr.fMode, mode);
	parms.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_CREATE_IF_NEW |
	    SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACCESS_READ;
	rc = VbglR0SfCreate(&vbox_client, &mnt->map, str, &parms);
	kmem_free(str, size);

	if (RT_FAILURE(rc))
		return (sfprov_vbox2errno(rc));
	if (parms.Handle == SHFL_HANDLE_NIL) {
		if (parms.Result == SHFL_FILE_EXISTS)
			return (EEXIST);
		return (ENOENT);
	}
	newfp = kmem_alloc(sizeof(sfp_file_t), KM_SLEEP);
	newfp->handle = parms.Handle;
	newfp->map = mnt->map;
	*fp = newfp;
	sfprov_stat_from_info(mnt, stat, &parms.Info);
	return (0);
}

int
sfprov_set_show_symlinks(void)
{
	int rc;

	rc = VbglR0SfSetSymlinks(&vbox_client);
	if (RT_FAILURE(rc))
		return (sfprov_vbox2errno(rc));

	return (0);
}

int
sfprov_remove(sfp_mount_t *mnt, char *path, uint_t is_link)
{
	int rc;
	SHFLSTRING *str;
	int size;

	str = sfprov_string(path, &size);
	rc = VbglR0SfRemove(&vbox_client, &mnt->map, str,
			    SHFL_REMOVE_FILE | (is_link ? SHFL_REMOVE_SYMLINK : 0));
	kmem_free(str, size);
	if (RT_FAILURE(rc))
		return (sfprov_vbox2errno(rc));
	return (0);
}

int
sfprov_readlink(
	sfp_mount_t *mnt,
	char *path,
	char *target,
	size_t tgt_size)
{
	int rc;
	SHFLSTRING *str;
	int size;

	str = sfprov_string(path, &size);

	rc = VbglR0SfReadLink(&vbox_client, &mnt->map, str, (uint32_t) tgt_size,
	    target);
	if (RT_FAILURE(rc))
		rc = sfprov_vbox2errno(rc);

	kmem_free(str, size);
	return (rc);
}

int
sfprov_symlink(
	sfp_mount_t *mnt,
	char *linkname,
	char *target,
	sffs_stat_t *stat)
{
	int rc;
	SHFLSTRING *lnk, *tgt;
	int lnk_size, tgt_size;
	SHFLFSOBJINFO info;

	lnk = sfprov_string(linkname, &lnk_size);
	tgt = sfprov_string(target, &tgt_size);

	rc = VbglR0SfSymlink(&vbox_client, &mnt->map, lnk, tgt, &info);
	if (RT_FAILURE(rc)) {
		rc = sfprov_vbox2errno(rc);
		goto done;
	}

	if (stat != NULL)
		sfprov_stat_from_info(mnt, stat, &info);

done:
	kmem_free(lnk, lnk_size);
	kmem_free(tgt, tgt_size);

	return (rc);
}

int
sfprov_rmdir(sfp_mount_t *mnt, char *path)
{
	int rc;
	SHFLSTRING *str;
	int size;

	str = sfprov_string(path, &size);
	rc = VbglR0SfRemove(&vbox_client, &mnt->map, str, SHFL_REMOVE_DIR);
	kmem_free(str, size);
	if (RT_FAILURE(rc))
		return (sfprov_vbox2errno(rc));
	return (0);
}

int
sfprov_rename(sfp_mount_t *mnt, char *from, char *to, uint_t is_dir)
{
	int rc;
	SHFLSTRING *old, *new;
	int old_size, new_size;

	old = sfprov_string(from, &old_size);
	new = sfprov_string(to, &new_size);
	rc = VbglR0SfRename(&vbox_client, &mnt->map, old, new,
			    (is_dir ? SHFL_RENAME_DIR : SHFL_RENAME_FILE) | SHFL_RENAME_REPLACE_IF_EXISTS);
	kmem_free(old, old_size);
	kmem_free(new, new_size);
	if (RT_FAILURE(rc))
		return (sfprov_vbox2errno(rc));
	return (0);
}


/*
 * Read all filenames in a directory.
 *
 * - success - all entries read and returned
 * - ENOENT - Couldn't open the directory for reading
 * - EINVAL - Internal error of some kind
 *
 * On successful return, *dirents points to a list of sffs_dirents_t;
 * for each dirent, all fields except the d_ino will be set appropriately.
 * The caller is responsible for freeing the dirents buffer.
 */
int
sfprov_readdir(
	sfp_mount_t *mnt,
	char *path,
	sffs_dirents_t **dirents,
	int flag)
{
	int error;
	char *cp;
	int len;
	SHFLSTRING *mask_str = NULL;	/* must be path with "/ *" appended */
	int mask_size;
	sfp_file_t *fp;
	uint32_t infobuff_alloc = 16384;
	SHFLDIRINFO *infobuff = NULL, *info;
	uint32_t numbytes;
	uint32_t nents;
	uint32_t size;
	off_t offset;
	sffs_dirents_t *cur_buf;
	struct sffs_dirent *dirent;
	unsigned short reclen;
	unsigned short entlen;

	*dirents = NULL;

	error = sfprov_diropen(mnt, path, &fp);
	if (error != 0)
		return (ENOENT);

	/*
	 * Allocate the first dirents buffers.
	 */
	*dirents = kmem_alloc(SFFS_DIRENTS_SIZE, KM_SLEEP);
	if (*dirents == NULL) {
		error = (ENOSPC);
		goto done;
	}
	cur_buf = *dirents;
	cur_buf->sf_next = NULL;
	cur_buf->sf_len = 0;

	/*
	 * Create mask that VBox expects. This needs to be the directory path,
	 * plus a "*" wildcard to get all files.
	 */
	len = strlen(path) + 3;
	cp = kmem_alloc(len, KM_SLEEP);
	if (cp == NULL) {
		error = (ENOSPC);
		goto done;
	}
	strcpy(cp, path);
	strcat(cp, "/*");
	mask_str = sfprov_string(cp, &mask_size);
	kmem_free(cp, len);

	/*
	 * Now loop using VbglR0SfDirInfo
	 */
	infobuff = kmem_alloc(infobuff_alloc, KM_SLEEP);
	if (infobuff == NULL) {
		error = (ENOSPC);
		goto done;
	}

	offset = 0;
	for (;;) {
		numbytes = infobuff_alloc;
		error = VbglR0SfDirInfo(&vbox_client, &fp->map, fp->handle,
					mask_str, 0, 0, &numbytes, infobuff, &nents);
		switch (error) {

		case VINF_SUCCESS:
			/* fallthrough */
		case VERR_NO_MORE_FILES:
			break;

		case VERR_NO_TRANSLATION:
			/* XXX ??? */
			break;

		default:
			error = sfprov_vbox2errno(error);
			goto done;
		}

		/*
		 * Create the dirent_t's and save the stats for each name
		 */
		for (info = infobuff; (char *) info < (char *) infobuff + numbytes; nents--) {
			/* expand buffers if we need more space */
			reclen = DIRENT64_RECLEN(strlen(info->name.String.utf8));
			entlen = sizeof(sffs_stat_t) + reclen;
			if (SFFS_DIRENTS_OFF + cur_buf->sf_len + entlen > SFFS_DIRENTS_SIZE) {
				cur_buf->sf_next = kmem_alloc(SFFS_DIRENTS_SIZE, KM_SLEEP);
				if (cur_buf->sf_next == NULL) {
					error = ENOSPC;
					goto done;
				}
				cur_buf = cur_buf->sf_next;
				cur_buf->sf_next = NULL;
				cur_buf->sf_len = 0;
			}

			/* create the dirent with the name, offset, and len */
			dirent = (struct sffs_dirent *)
			    (((char *) &cur_buf->sf_entries[0]) + cur_buf->sf_len);
			strncpy(&dirent->sf_entry.d_name[0], info->name.String.utf8, DIRENT64_NAMELEN(reclen));
			dirent->sf_entry.d_reclen = reclen;
			offset += entlen;
			dirent->sf_entry.d_off = offset;

			/* save the stats */
			sfprov_stat_from_info(mnt, &dirent->sf_stat, &info->Info);

			/* next info */
			cur_buf->sf_len += entlen;
			size = offsetof (SHFLDIRINFO, name.String) + info->name.u16Size;
			info = (SHFLDIRINFO *) ((uintptr_t) info + size);
		}
		ASSERT(nents == 0);
		ASSERT((char *) info == (char *) infobuff + numbytes);

		if (error == VERR_NO_MORE_FILES)
			break;
	}
	error = 0;

done:
	if (error != 0) {
		while (*dirents) {
			cur_buf = (*dirents)->sf_next;
			kmem_free(*dirents, SFFS_DIRENTS_SIZE);
			*dirents = cur_buf;
		}
	}
	if (infobuff != NULL)
		kmem_free(infobuff, infobuff_alloc);
	if (mask_str != NULL)
		kmem_free(mask_str, mask_size);
	sfprov_close(fp);
	return (error);
}
