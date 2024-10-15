/* $Id: vboxfs_vnode.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox File System for Solaris Guests, VNode header.
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

#ifndef GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_vnode_h
#define GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_vnode_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <sys/t_lock.h>
#include <sys/avl.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * sfnode is the file system dependent vnode data for vboxsf.
 * sfnode's also track all files ever accessed, both open and closed.
 * It duplicates some of the information in vnode, since it holds
 * information for files that may have been completely closed.
 *
 * The sfnode_t's are stored in an AVL tree sorted by:
 *	sf_sffs, sf_path
 */
typedef struct sfnode {
	avl_node_t	sf_linkage;	/* AVL tree linkage */
	struct sffs_data *sf_sffs;	/* containing mounted file system */
	char		*sf_path;	/* full pathname to file or dir */
	uint64_t	sf_ino;		/* assigned unique ID number */
	vnode_t		*sf_vnode;	/* vnode if active */
	sfp_file_t	*sf_file;	/* non NULL if open */
	int			sf_flag;    /* last opened file-mode. */
	struct sfnode	*sf_parent;	/* parent sfnode of this one */
	uint16_t	sf_children;	/* number of children sfnodes */
	uint8_t		sf_type;	/* VDIR or VREG */
	uint8_t		sf_is_stale;	/* this is stale and should be purged */
	sffs_stat_t	sf_stat;	/* cached file attrs for this node */
	uint64_t	sf_stat_time;	/* last-modified time of sf_stat */
	sffs_dirents_t	*sf_dir_list;	/* list of entries for this directory */
} sfnode_t;

#define VN2SFN(vp) ((sfnode_t *)(vp)->v_data)

#ifdef _KERNEL
extern int sffs_vnode_init(void);
extern void sffs_vnode_fini(void);
extern sfnode_t *sfnode_make(struct sffs_data *, char *, vtype_t, sfp_file_t *,
    sfnode_t *parent, sffs_stat_t *, uint64_t stat_time);
extern vnode_t *sfnode_get_vnode(sfnode_t *);

/*
 * Purge all cached information about a shared file system at unmount
 */
extern int sffs_purge(struct sffs_data *);

extern kmutex_t sffs_lock;
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* !GA_INCLUDED_SRC_solaris_SharedFolders_vboxfs_vnode_h */
