/* $Id: vboxfs_prov.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox File System for Solaris Guests, provider header.
 * Portions contributed by: Ronald.
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

#ifndef GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_prov_h
#define GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_prov_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/VBoxGuestLibSharedFolders.h>

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * These are the provider interfaces used by sffs to access the underlying
 * shared file system.
 */
#define	SFPROV_VERSION	1

/*
 * Initialization and termination.
 * sfprov_connect() is called once before any other interfaces and returns
 * a handle used in further calls. The argument should be SFPROV_VERSION
 * from above. On failure it returns a NULL pointer.
 *
 * sfprov_disconnect() must only be called after all sf file systems have been
 * unmounted.
 */
typedef struct sfp_connection sfp_connection_t;

extern sfp_connection_t *sfprov_connect(int);
extern void sfprov_disconnect(sfp_connection_t *);


/*
 * Mount / Unmount a shared folder.
 *
 * sfprov_mount() takes as input the connection pointer and the name of
 * the shared folder. On success, it returns zero and supplies an
 * sfp_mount_t handle. On failure it returns any relevant errno value.
 *
 * sfprov_unmount() unmounts the mounted file system. It returns 0 on
 * success and any relevant errno on failure.
 *
 * spf_mount_t is the representation of an active mount point.
 */
typedef struct spf_mount_t {
	VBGLSFMAP	map;		/**< guest<->host mapping */
	uid_t		sf_uid;		/**< owner of the mount point */
	gid_t		sf_gid;		/**< group of the mount point */
	mode_t		sf_dmode;   /**< mode of all directories if != ~0U */
	mode_t		sf_fmode;   /**< mode of all files if != ~0U */
	mode_t		sf_dmask;   /**< mask of all directories */
	mode_t		sf_fmask;   /**< mask of all files */
} sfp_mount_t;

extern int sfprov_mount(sfp_connection_t *, char *, sfp_mount_t **);
extern int sfprov_unmount(sfp_mount_t *);

/*
 * query information about a mounted file system
 */
typedef struct sffs_fsinfo {
	uint64_t blksize;
	uint64_t blksused;
	uint64_t blksavail;
	uint32_t maxnamesize;
	uint32_t readonly;
} sffs_fsinfo_t;

extern int sfprov_get_fsinfo(sfp_mount_t *, sffs_fsinfo_t *);

/*
 * File operations: open/close/read/write/etc.
 *
 * open/create can return any relevant errno, however ENOENT
 * generally means that the host file didn't exist.
 */
typedef struct sffs_stat {
	mode_t		sf_mode;
	off_t		sf_size;
	off_t		sf_alloc;
	timestruc_t	sf_atime;
	timestruc_t	sf_mtime;
	timestruc_t	sf_ctime;
} sffs_stat_t;

typedef struct sfp_file sfp_file_t;

extern int sfprov_create(sfp_mount_t *, char *path, mode_t mode,
    sfp_file_t **fp, sffs_stat_t *stat);
extern int sfprov_diropen(sfp_mount_t *mnt, char *path, sfp_file_t **fp);
extern int sfprov_open(sfp_mount_t *, char *path, sfp_file_t **fp, int flag);
extern int sfprov_close(sfp_file_t *fp);
extern int sfprov_read(sfp_file_t *, char * buffer, uint64_t offset,
    uint32_t *numbytes);
extern int sfprov_write(sfp_file_t *, char * buffer, uint64_t offset,
    uint32_t *numbytes);
extern int sfprov_fsync(sfp_file_t *fp);


/*
 * get/set information about a file (or directory) using pathname
 */
extern int sfprov_get_mode(sfp_mount_t *, char *, mode_t *);
extern int sfprov_get_size(sfp_mount_t *, char *, uint64_t *);
extern int sfprov_get_atime(sfp_mount_t *, char *, timestruc_t *);
extern int sfprov_get_mtime(sfp_mount_t *, char *, timestruc_t *);
extern int sfprov_get_ctime(sfp_mount_t *, char *, timestruc_t *);
extern int sfprov_get_attr(sfp_mount_t *, char *, sffs_stat_t *);
extern int sfprov_set_attr(sfp_mount_t *, char *, uint_t, mode_t,
   timestruc_t, timestruc_t, timestruc_t);
extern int sfprov_set_size(sfp_mount_t *, char *, uint64_t);


/*
 * File/Directory operations
 */
extern int sfprov_remove(sfp_mount_t *, char *path, uint_t is_link);
extern int sfprov_mkdir(sfp_mount_t *, char *path, mode_t mode,
    sfp_file_t **fp, sffs_stat_t *stat);
extern int sfprov_rmdir(sfp_mount_t *, char *path);
extern int sfprov_rename(sfp_mount_t *, char *from, char *to, uint_t is_dir);


/*
 * Symbolic link operations
 */
extern int sfprov_set_show_symlinks(void);
extern int sfprov_readlink(sfp_mount_t *, char *path, char *target,
    size_t tgt_size);
extern int sfprov_symlink(sfp_mount_t *, char *linkname, char *target,
    sffs_stat_t *stat);


/*
 * Read directory entries.
 */
/*
 * a singly linked list of buffers, each containing an array of stat's+dirent's.
 * sf_len is length of the sf_entries array, in bytes.
 */
typedef struct sffs_dirents {
	struct sffs_dirents	*sf_next;
	len_t			sf_len;
	struct sffs_dirent {
		sffs_stat_t	sf_stat;
		dirent64_t	sf_entry;	/* this is variable length */
	}			sf_entries[1];
} sffs_dirents_t;

#define SFFS_DIRENTS_SIZE	8192
#define SFFS_DIRENTS_OFF	(offsetof(sffs_dirents_t, sf_entries[0]))

extern int sfprov_readdir(sfp_mount_t *mnt, char *path,
	sffs_dirents_t **dirents, int flag);

#ifdef	__cplusplus
}
#endif

#endif /* !GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_prov_h */

