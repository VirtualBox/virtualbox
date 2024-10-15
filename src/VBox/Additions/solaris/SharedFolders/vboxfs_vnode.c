/* $Id: vboxfs_vnode.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox File System for Solaris Guests, vnode implementation.
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

/*
 * Shared Folder File System is used from Solaris when run as a guest operating
 * system on VirtualBox, though is meant to be usable with any hypervisor that
 * can provide similar functionality. The sffs code handles all the Solaris
 * specific semantics and relies on a provider module to actually access
 * directories, files, etc. The provider interfaces are described in
 * "vboxfs_prov.h" and the module implementing them is shipped as part of the
 * VirtualBox Guest Additions for Solaris.
 *
 * The shared folder file system is similar to a networked file system,
 * but with some caveats. The sffs code caches minimal information and proxies
 * out to the provider whenever possible. Here are some things that are
 * handled in this code and not by the proxy:
 *
 * - a way to open ".." from any already open directory
 * - st_ino numbers
 * - detecting directory changes that happened on the host.
 *
 * The implementation builds a cache of information for every file/directory
 * ever accessed in all mounted sffs filesystems using sf_node structures.
 *
 * This information for both open or closed files can become invalid if
 * asynchronous changes are made on the host. Solaris should not panic() in
 * this event, but some file system operations may return unexpected errors.
 * Information for such directories or files while they have active vnodes
 * is removed from the regular cache and stored in a "stale" bucket until
 * the vnode becomes completely inactive.
 *
 * We suppport only read-only mmap (VBOXVFS_WITH_MMAP) i.e. MAP_SHARED,
 * MAP_PRIVATE in PROT_READ, this data caching would not be coherent with
 * normal simultaneous read()/write() operations, nor will it be coherent
 * with data access on the host. Writable mmap(MAP_SHARED) access is not
 * implemented, as guaranteeing any kind of coherency with concurrent
 * activity on the host would be near impossible with the existing
 * interfaces.
 *
 * A note about locking. sffs is not a high performance file system.
 * No fine grained locking is done. The one sffs_lock protects just about
 * everything.
 */

#include <VBox/log.h>
#include <iprt/asm.h>

#include <unistd.h>
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
#include <sys/vfs.h>
#include <sys/vmsystm.h>
#include <vm/seg_kpm.h>
#include <vm/pvn.h>
#if !defined(VBOX_VFS_SOLARIS_10U6)
# include <sys/vfs_opreg.h>
#endif
#include <sys/pathname.h>
#include <sys/dirent.h>
#include <sys/fs_subr.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#undef u /* /usr/include/sys/user.h:249:1 is where this is defined to (curproc->p_user). very cool. */

#include "vboxfs_prov.h"
#include "vboxfs_vnode.h"
#include "vboxfs_vfs.h"

/*
 * Solaris 11u1b10 Extended Policy putback CR 7121445 removes secpolicy_vnode_access from sys/policy.h
 */
#ifdef VBOX_VFS_EXTENDED_POLICY
int secpolicy_vnode_access(const cred_t *, vnode_t *, uid_t, mode_t);
#endif

#define VBOXVFS_WITH_MMAP

static struct vnodeops *sffs_ops = NULL;

kmutex_t sffs_lock;
static avl_tree_t sfnodes;
static avl_tree_t stale_sfnodes;

/*
 * For now we'll use an I/O buffer that doesn't page fault for VirtualBox
 * to transfer data into.
 */
char *sffs_buffer;

/*
 * sfnode_compare() is needed for AVL tree functionality.
 * The nodes are sorted by mounted filesystem, then path. If the
 * nodes are stale, the node pointer itself is used to force uniqueness.
 */
static int
sfnode_compare(const void *a, const void *b)
{
	sfnode_t *x = (sfnode_t *)a;
	sfnode_t *y = (sfnode_t *)b;
	int diff;

	if (x->sf_is_stale) {
		ASSERT(y->sf_is_stale);
		diff = strcmp(x->sf_path, y->sf_path);
		if (diff == 0)
			diff = (uintptr_t)y - (uintptr_t)x;
	} else {
		ASSERT(!y->sf_is_stale);
		diff = (uintptr_t)y->sf_sffs - (uintptr_t)x->sf_sffs;
		if (diff == 0)
			diff = strcmp(x->sf_path, y->sf_path);
	}
	if (diff < 0)
		return (-1);
	if (diff > 0)
		return (1);
	return (0);
}

/*
 * Construct a new pathname given an sfnode plus an optional tail component.
 * This handles ".." and "."
 */
static char *
sfnode_construct_path(sfnode_t *node, char *tail)
{
	char *p;

	if (strcmp(tail, ".") == 0 || strcmp(tail, "..") == 0)
		panic("construct path for %s", tail);
	p = kmem_alloc(strlen(node->sf_path) + 1 + strlen(tail) + 1, KM_SLEEP);
	strcpy(p, node->sf_path);
	strcat(p, "/");
	strcat(p, tail);
	return (p);
}

/*
 * Clears the (cached) directory listing for the node.
 */
static void
sfnode_clear_dir_list(sfnode_t *node)
{
	ASSERT(MUTEX_HELD(&sffs_lock));

	while (node->sf_dir_list != NULL) {
		sffs_dirents_t *next = node->sf_dir_list->sf_next;
		kmem_free(node->sf_dir_list, SFFS_DIRENTS_SIZE);
		node->sf_dir_list = next;
	}
}

/*
 * Open the provider file associated with a vnode. Holding the file open is
 * the only way we have of trying to have a vnode continue to refer to the
 * same host file in the host in light of the possibility of host side renames.
 */
static void
sfnode_open(sfnode_t *node, int flag)
{
	int error;
	sfp_file_t *fp;

	if (node->sf_file != NULL)
		return;
	error = sfprov_open(node->sf_sffs->sf_handle, node->sf_path, &fp, flag);
	if (error == 0)
	{
		node->sf_file = fp;
		node->sf_flag = flag;
	}
	else
		node->sf_flag = ~0;
}

/*
 * get a new vnode reference for an sfnode
 */
vnode_t *
sfnode_get_vnode(sfnode_t *node)
{
	vnode_t *vp;

	if (node->sf_vnode != NULL) {
		VN_HOLD(node->sf_vnode);
	} else {
		vp = vn_alloc(KM_SLEEP);
        LogFlowFunc(("  %s gets vnode 0x%p\n", node->sf_path, vp));
		vp->v_type = node->sf_type;
		vp->v_vfsp = node->sf_sffs->sf_vfsp;
		vn_setops(vp, sffs_ops);
		vp->v_flag = VNOSWAP;
#ifndef VBOXVFS_WITH_MMAP
		vp->v_flag |= VNOMAP;
#endif
		vn_exists(vp);
		vp->v_data = node;
		node->sf_vnode = vp;
	}
	return (node->sf_vnode);
}

/*
 * Allocate and initialize a new sfnode and assign it a vnode
 */
sfnode_t *
sfnode_make(
	sffs_data_t	*sffs,
	char		*path,
	vtype_t		type,
	sfp_file_t	*fp,
	sfnode_t	*parent,	/* can be NULL for root */
	sffs_stat_t	*stat,
	uint64_t	stat_time)
{
	sfnode_t	*node;
	avl_index_t	where;

	ASSERT(MUTEX_HELD(&sffs_lock));
	ASSERT(path != NULL);

	/*
	 * build the sfnode
	 */
    LogFlowFunc(("sffs_make(%s)\n", path));
	node = kmem_alloc(sizeof (*node), KM_SLEEP);
	node->sf_sffs = sffs;
	VFS_HOLD(node->sf_sffs->sf_vfsp);
	node->sf_path = path;
	node->sf_ino = sffs->sf_ino++;
	node->sf_type = type;
	node->sf_is_stale = 0;	/* never stale at creation */
	node->sf_file = fp;
	node->sf_flag = ~0;
	node->sf_vnode = NULL;	/* do this before any sfnode_get_vnode() */
	node->sf_children = 0;
	node->sf_parent = parent;
	if (parent)
		++parent->sf_children;
	node->sf_dir_list = NULL;
	if (stat != NULL) {
		node->sf_stat = *stat;
		node->sf_stat_time = stat_time;
	} else {
		node->sf_stat_time = 0;
	}

	/*
	 * add the new node to our cache
	 */
	if (avl_find(&sfnodes, node, &where) != NULL)
		panic("sffs_create_sfnode(%s): duplicate sfnode_t", path);
	avl_insert(&sfnodes, node, where);
	return (node);
}

/*
 * destroy an sfnode
 */
static void
sfnode_destroy(sfnode_t *node)
{
	avl_index_t where;
	avl_tree_t *tree;
	sfnode_t *parent;
top:
	parent = node->sf_parent;
	ASSERT(MUTEX_HELD(&sffs_lock));
	ASSERT(node->sf_path != NULL);
    LogFlowFunc(("sffs_destroy(%s)%s\n", node->sf_path, node->sf_is_stale ? " stale": ""));
	if (node->sf_children != 0)
		panic("sfnode_destroy(%s) has %d children", node->sf_path, node->sf_children);
	if (node->sf_vnode != NULL)
		panic("sfnode_destroy(%s) has active vnode", node->sf_path);

	if (node->sf_is_stale)
		tree = &stale_sfnodes;
	else
		tree = &sfnodes;
	if (avl_find(tree, node, &where) == NULL)
		panic("sfnode_destroy(%s) not found", node->sf_path);
	avl_remove(tree, node);

	VFS_RELE(node->sf_sffs->sf_vfsp);
	sfnode_clear_dir_list(node);
	kmem_free(node->sf_path, strlen(node->sf_path) + 1);
	kmem_free(node, sizeof (*node));
	if (parent != NULL) {
		sfnode_clear_dir_list(parent);
		if (parent->sf_children == 0)
			panic("sfnode_destroy parent (%s) has no child", parent->sf_path);
		--parent->sf_children;
		if (parent->sf_children == 0 &&
		    parent->sf_is_stale &&
		    parent->sf_vnode == NULL) {
			node = parent;
			goto top;
		}
	}
}

/*
 * Some sort of host operation on an sfnode has failed or it has been
 * deleted. Mark this node and any children as stale, deleting knowledge
 * about any which do not have active vnodes or children
 * This also handle deleting an inactive node that was already stale.
 */
static void
sfnode_make_stale(sfnode_t *node)
{
	sfnode_t *n;
	int len;
	ASSERT(MUTEX_HELD(&sffs_lock));
	avl_index_t where;

	/*
	 * First deal with any children of a directory node.
	 * If a directory becomes stale, anything below it becomes stale too.
	 */
	if (!node->sf_is_stale && node->sf_type == VDIR) {
		len = strlen(node->sf_path);

		n = node;
		while ((n = AVL_NEXT(&sfnodes, node)) != NULL) {
			ASSERT(!n->sf_is_stale);

			/*
			 * quit when no longer seeing children of node
			 */
			if (n->sf_sffs != node->sf_sffs ||
			    strncmp(node->sf_path, n->sf_path, len) != 0 ||
			    n->sf_path[len] != '/')
				break;

			/*
			 * Either mark the child as stale or destroy it
			 */
			if (n->sf_vnode == NULL && n->sf_children == 0) {
				sfnode_destroy(n);
			} else {
                LogFlowFunc(("sffs_make_stale(%s) sub\n", n->sf_path));
				sfnode_clear_dir_list(n);
				if (avl_find(&sfnodes, n, &where) == NULL)
					panic("sfnode_make_stale(%s)"
					    " not in sfnodes", n->sf_path);
				avl_remove(&sfnodes, n);
				n->sf_is_stale = 1;
				if (avl_find(&stale_sfnodes, n, &where) != NULL)
					panic("sffs_make_stale(%s) duplicates",
					    n->sf_path);
				avl_insert(&stale_sfnodes, n, where);
			}
		}
	}

	/*
	 * Now deal with the given node.
	 */
	if (node->sf_vnode == NULL && node->sf_children == 0) {
		sfnode_destroy(node);
	} else if (!node->sf_is_stale) {
        LogFlowFunc(("sffs_make_stale(%s)\n", node->sf_path));
		sfnode_clear_dir_list(node);
		if (node->sf_parent)
			sfnode_clear_dir_list(node->sf_parent);
		if (avl_find(&sfnodes, node, &where) == NULL)
			panic("sfnode_make_stale(%s) not in sfnodes",
			    node->sf_path);
		avl_remove(&sfnodes, node);
		node->sf_is_stale = 1;
		if (avl_find(&stale_sfnodes, node, &where) != NULL)
			panic("sffs_make_stale(%s) duplicates", node->sf_path);
		avl_insert(&stale_sfnodes, node, where);
	}
}

static uint64_t
sfnode_cur_time_usec(void)
{
	clock_t now = drv_hztousec(ddi_get_lbolt());
	return now;
}

static int
sfnode_stat_cached(sfnode_t *node)
{
	return (sfnode_cur_time_usec() - node->sf_stat_time) <
	    node->sf_sffs->sf_stat_ttl * 1000L;
}

static void
sfnode_invalidate_stat_cache(sfnode_t *node)
{
	node->sf_stat_time = 0;
}

static int
sfnode_update_stat_cache(sfnode_t *node)
{
	int error;

	error = sfprov_get_attr(node->sf_sffs->sf_handle, node->sf_path,
	    &node->sf_stat);
	if (error == ENOENT)
		sfnode_make_stale(node);
	if (error == 0)
		node->sf_stat_time = sfnode_cur_time_usec();

	return (error);
}

/*
 * Rename a file or a directory
 */
static void
sfnode_rename(sfnode_t *node, sfnode_t *newparent, char *path)
{
	sfnode_t *n;
	sfnode_t template;
	avl_index_t where;
	int len = strlen(path);
	int old_len;
	char *new_path;
	char *tail;
	ASSERT(MUTEX_HELD(&sffs_lock));

	ASSERT(!node->sf_is_stale);

	/*
	 * Have to remove anything existing that had the new name.
	 */
	template.sf_sffs = node->sf_sffs;
	template.sf_path = path;
	template.sf_is_stale = 0;
	n = avl_find(&sfnodes, &template, &where);
	if (n != NULL)
		sfnode_make_stale(n);

	/*
	 * Do the renaming, deal with any children of this node first.
	 */
	if (node->sf_type == VDIR) {
		old_len = strlen(node->sf_path);
		while ((n = AVL_NEXT(&sfnodes, node)) != NULL) {

			/*
			 * quit when no longer seeing children of node
			 */
			if (n->sf_sffs != node->sf_sffs ||
			    strncmp(node->sf_path, n->sf_path, old_len) != 0 ||
			    n->sf_path[old_len] != '/')
				break;

			/*
			 * Rename the child:
			 * - build the new path name
			 * - unlink the AVL node
			 * - assign the new name
			 * - re-insert the AVL name
			 */
			ASSERT(strlen(n->sf_path) > old_len);
			tail = n->sf_path + old_len; /* includes initial "/" */
			new_path = kmem_alloc(len + strlen(tail) + 1,
			    KM_SLEEP);
			strcpy(new_path, path);
			strcat(new_path, tail);
			if (avl_find(&sfnodes, n, &where) == NULL)
				panic("sfnode_rename(%s) not in sfnodes",
				   n->sf_path);
			avl_remove(&sfnodes, n);
            LogFlowFunc(("sfnode_rname(%s to %s) sub\n", n->sf_path, new_path));
			kmem_free(n->sf_path, strlen(n->sf_path) + 1);
			n->sf_path = new_path;
			if (avl_find(&sfnodes, n, &where) != NULL)
				panic("sfnode_rename(%s) duplicates",
				    n->sf_path);
			avl_insert(&sfnodes, n, where);
		}
	}

	/*
	 * Deal with the given node.
	 */
	if (avl_find(&sfnodes, node, &where) == NULL)
		panic("sfnode_rename(%s) not in sfnodes", node->sf_path);
	avl_remove(&sfnodes, node);
    LogFlowFunc(("sfnode_rname(%s to %s)\n", node->sf_path, path));
	kmem_free(node->sf_path, strlen(node->sf_path) + 1);
	node->sf_path = path;
	if (avl_find(&sfnodes, node, &where) != NULL)
		panic("sfnode_rename(%s) duplicates", node->sf_path);
	avl_insert(&sfnodes, node, where);

	/*
	 * change the parent
	 */
	if (node->sf_parent == NULL)
		panic("sfnode_rename(%s) no parent", node->sf_path);
	if (node->sf_parent->sf_children == 0)
		panic("sfnode_rename(%s) parent has no child", node->sf_path);
	sfnode_clear_dir_list(node->sf_parent);
	sfnode_clear_dir_list(newparent);
	--node->sf_parent->sf_children;
	node->sf_parent = newparent;
	++newparent->sf_children;
}

/*
 * Look for a cached node, if not found either handle ".." or try looking
 * via the provider. Create an entry in sfnodes if found but not cached yet.
 * If the create flag is set, a file or directory is created. If the file
 * already existed, an error is returned.
 * Nodes returned from this routine always have a vnode with its ref count
 * bumped by 1.
 */
static sfnode_t *
sfnode_lookup(
	sfnode_t *dir,
	char *name,
	vtype_t create,
	mode_t c_mode,
	sffs_stat_t *stat,
	uint64_t stat_time,
	int *err)
{
	avl_index_t	where;
	sfnode_t	template;
	sfnode_t	*node;
	int		error = 0;
	int		type;
	char		*fullpath;
	sfp_file_t	*fp;
	sffs_stat_t	tmp_stat;

	ASSERT(MUTEX_HELD(&sffs_lock));

	if (err)
		*err = error;

	/*
	 * handle referencing myself
	 */
	if (strcmp(name, "") == 0 || strcmp(name, ".") == 0)
		return (dir);

	/*
	 * deal with parent
	 */
	if (strcmp(name, "..") == 0)
		return (dir->sf_parent);

	/*
	 * Look for an existing node.
	 */
	fullpath = sfnode_construct_path(dir, name);
	template.sf_sffs = dir->sf_sffs;
	template.sf_path = fullpath;
	template.sf_is_stale = 0;
	node = avl_find(&sfnodes, &template, &where);
	if (node != NULL) {
		kmem_free(fullpath, strlen(fullpath) + 1);
		if (create != VNON)
			return (NULL);
		return (node);
	}

	/*
	 * No entry for this path currently.
	 * Check if the file exists with the provider and get the type from
	 * there.
	 */
	if (create == VREG) {
		type = VREG;
		stat = &tmp_stat;
		error = sfprov_create(dir->sf_sffs->sf_handle, fullpath, c_mode,
					&fp, stat);
		stat_time = sfnode_cur_time_usec();
	} else if (create == VDIR) {
		type = VDIR;
		stat = &tmp_stat;
		error = sfprov_mkdir(dir->sf_sffs->sf_handle, fullpath, c_mode,
					&fp, stat);
		stat_time = sfnode_cur_time_usec();
	} else {
		mode_t m;
		fp = NULL;
		type = VNON;
		if (stat == NULL) {
			stat = &tmp_stat;
			error = sfprov_get_attr(dir->sf_sffs->sf_handle,
			    fullpath, stat);
			stat_time = sfnode_cur_time_usec();
		} else {
			error = 0;
		}
		m = stat->sf_mode;
		if (error != 0)
			error = ENOENT;
		else if (S_ISDIR(m))
			type = VDIR;
		else if (S_ISREG(m))
			type = VREG;
		else if (S_ISLNK(m))
			type = VLNK;
	}

	if (err)
		*err = error;

	/*
	 * If no errors, make a new node and return it.
	 */
	if (error) {
		kmem_free(fullpath, strlen(fullpath) + 1);
		return (NULL);
	}
	node = sfnode_make(dir->sf_sffs, fullpath, type, fp, dir, stat,
	    stat_time);
	return (node);
}


/*
 * uid and gid in sffs determine owner and group for all files.
 */
static int
sfnode_access(sfnode_t *node, mode_t mode, cred_t *cr)
{
	sffs_data_t *sffs = node->sf_sffs;
	mode_t m;
	int shift = 0;
	int error;
	vnode_t *vp;

	ASSERT(MUTEX_HELD(&sffs_lock));

	/*
	 * get the mode from the cache or provider
	 */
	if (sfnode_stat_cached(node))
		error = 0;
	else
		error = sfnode_update_stat_cache(node);
	m = (error == 0) ? (node->sf_stat.sf_mode & MODEMASK) : 0;

	/*
	 * mask off the permissions based on uid/gid
	 */
	if (crgetuid(cr) != sffs->sf_handle->sf_uid) {
		shift += 3;
		if (groupmember(sffs->sf_handle->sf_gid, cr) == 0)
			shift += 3;
	}
	mode &= ~(m << shift);

	if (mode == 0) {
		error = 0;
	} else {
		/** @todo r=ramshankar: This can probably be optimized by holding static vnode
		 *  	  templates for dir/file, as it only checks the type rather than
		 *  	  fetching/allocating the real vnode. */
		vp = sfnode_get_vnode(node);
		error = secpolicy_vnode_access(cr, vp, sffs->sf_handle->sf_uid, mode);
		VN_RELE(vp);
	}
	return (error);
}


/*
 *
 * Everything below this point are the vnode operations used by Solaris VFS
 */
static int
sffs_readdir(
	vnode_t		*vp,
	uio_t		*uiop,
	cred_t		*cred,
	int		*eofp
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct,
	int		flag
#endif
	)
{
	sfnode_t *dir = VN2SFN(vp);
	sfnode_t *node;
	struct sffs_dirent *dirent = NULL;
	sffs_dirents_t *cur_buf;
	offset_t offset = 0;
	offset_t orig_off = uiop->uio_loffset;
	int dummy_eof;
	int error = 0;

	if (uiop->uio_iovcnt != 1)
		return (EINVAL);

	if (vp->v_type != VDIR)
		return (ENOTDIR);

	if (eofp == NULL)
		eofp = &dummy_eof;
	*eofp = 0;

	if (uiop->uio_loffset >= MAXOFFSET_T) {
		*eofp = 1;
		return (0);
	}

	/*
	 * Get the directory entry names from the host. This gets all
	 * entries. These are stored in a linked list of sffs_dirents_t
	 * buffers, each of which contains a list of dirent64_t's.
	 */
	mutex_enter(&sffs_lock);

	if (dir->sf_dir_list == NULL) {
#if defined(VBOX_VFS_SOLARIS_10U6)
        int flag = 0;
#endif
		error = sfprov_readdir(dir->sf_sffs->sf_handle, dir->sf_path,
		    &dir->sf_dir_list, flag);
		if (error != 0)
			goto done;
	}

 	/*
	 * Validate and skip to the desired offset.
	 */
	cur_buf = dir->sf_dir_list;
	offset = 0;

	while (cur_buf != NULL &&
	    offset + cur_buf->sf_len <= uiop->uio_loffset) {
		offset += cur_buf->sf_len;
		cur_buf = cur_buf->sf_next;
	}

	if (cur_buf == NULL && offset != uiop->uio_loffset) {
		error = EINVAL;
		goto done;
	}
	if (cur_buf != NULL && offset != uiop->uio_loffset) {
		offset_t off = offset;
		int step;
		dirent = &cur_buf->sf_entries[0];

		while (off < uiop->uio_loffset) {
			if (dirent->sf_entry.d_off == uiop->uio_loffset)
				break;
			step = sizeof(sffs_stat_t) + dirent->sf_entry.d_reclen;
			dirent = (struct sffs_dirent *) (((char *) dirent) + step);
			off += step;
		}

		if (off >= uiop->uio_loffset) {
			error = EINVAL;
			goto done;
		}
	}

	offset = uiop->uio_loffset - offset;

	/*
	 * Lookup each of the names, so that we have ino's, and copy to
	 * result buffer.
	 */
	while (cur_buf != NULL) {
		if (offset >= cur_buf->sf_len) {
			cur_buf = cur_buf->sf_next;
			offset = 0;
			continue;
		}

		dirent = (struct sffs_dirent *)
		    (((char *) &cur_buf->sf_entries[0]) + offset);
		if (dirent->sf_entry.d_reclen > uiop->uio_resid)
			break;

		if (strcmp(dirent->sf_entry.d_name, ".") == 0) {
			node = dir;
		} else if (strcmp(dirent->sf_entry.d_name, "..") == 0) {
			node = dir->sf_parent;
			if (node == NULL)
				node = dir;
		} else {
			node = sfnode_lookup(dir, dirent->sf_entry.d_name, VNON,
				0, &dirent->sf_stat, sfnode_cur_time_usec(), NULL);
			if (node == NULL)
				panic("sffs_readdir() lookup failed");
		}
		dirent->sf_entry.d_ino = node->sf_ino;

		error = uiomove(&dirent->sf_entry, dirent->sf_entry.d_reclen, UIO_READ, uiop);
		if (error != 0)
			break;

		uiop->uio_loffset= dirent->sf_entry.d_off;
		offset += sizeof(sffs_stat_t) + dirent->sf_entry.d_reclen;
	}
	if (error == 0 && cur_buf == NULL)
		*eofp = 1;
done:
	mutex_exit(&sffs_lock);
	if (error != 0)
		uiop->uio_loffset = orig_off;
	return (error);
}


/*
 * HERE JOE.. this may need more logic, need to look at other file systems
 */
static int
sffs_pathconf(
	vnode_t	*vp,
	int	cmd,
	ulong_t	*valp,
	cred_t	*cr
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
#if !defined(VBOX_VFS_SOLARIS_10U6)
	return (fs_pathconf(vp, cmd, valp, cr, ct));
#else
	return (fs_pathconf(vp, cmd, valp, cr));
#endif
}

static int
sffs_getattr(
	vnode_t		*vp,
	vattr_t		*vap,
	int		flags,
	cred_t		*cred
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
	sfnode_t	*node = VN2SFN(vp);
	sffs_data_t	*sffs = node->sf_sffs;
	mode_t		mode;
	int		error = 0;

	mutex_enter(&sffs_lock);
	vap->va_type = vp->v_type;
	vap->va_uid = sffs->sf_handle->sf_uid;
	vap->va_gid = sffs->sf_handle->sf_gid;
	vap->va_fsid = sffs->sf_vfsp->vfs_dev;
	vap->va_nodeid = node->sf_ino;
	vap->va_nlink = 1;
	vap->va_rdev =  sffs->sf_vfsp->vfs_dev;
	vap->va_seq = 0;

	if (!sfnode_stat_cached(node)) {
		error = sfnode_update_stat_cache(node);
		if (error != 0)
			goto done;
	}

	vap->va_atime = node->sf_stat.sf_atime;
	vap->va_mtime = node->sf_stat.sf_mtime;
	vap->va_ctime = node->sf_stat.sf_ctime;

	mode = node->sf_stat.sf_mode;
	vap->va_mode = mode & MODEMASK;

	vap->va_size = node->sf_stat.sf_size;
	vap->va_blksize = 512;
	vap->va_nblocks = (node->sf_stat.sf_alloc + 511) / 512;

done:
	mutex_exit(&sffs_lock);
	return (error);
}

static int
sffs_setattr(
	vnode_t		*vp,
	vattr_t		*vap,
	int		flags,
	cred_t		*cred,
	caller_context_t *ct)
{
	sfnode_t	*node = VN2SFN(vp);
	int		error;
	mode_t		mode;

	mode = vap->va_mode;
	if (vp->v_type == VREG)
		mode |= S_IFREG;
	else if (vp->v_type == VDIR)
		mode |= S_IFDIR;
	else if (vp->v_type == VBLK)
		mode |= S_IFBLK;
	else if (vp->v_type == VCHR)
		mode |= S_IFCHR;
	else if (vp->v_type == VLNK)
		mode |= S_IFLNK;
	else if (vp->v_type == VFIFO)
		mode |= S_IFIFO;
	else if (vp->v_type == VSOCK)
		mode |= S_IFSOCK;

	mutex_enter(&sffs_lock);

	sfnode_invalidate_stat_cache(node);
	error = sfprov_set_attr(node->sf_sffs->sf_handle, node->sf_path,
	    vap->va_mask, mode, vap->va_atime, vap->va_mtime, vap->va_ctime);
	if (error == ENOENT)
		sfnode_make_stale(node);

	mutex_exit(&sffs_lock);
	return (error);
}

static int
sffs_space(
	vnode_t		*vp,
	int		cmd,
	struct flock64	*bfp,
	int		flags,
	offset_t	off,
	cred_t		*cred,
	caller_context_t *ct)
{
	sfnode_t	*node = VN2SFN(vp);
	int		error;

	/* we only support changing the length of the file */
	if (bfp->l_whence != SEEK_SET || bfp->l_len != 0)
		return ENOSYS;

	mutex_enter(&sffs_lock);

	sfnode_invalidate_stat_cache(node);

	error = sfprov_set_size(node->sf_sffs->sf_handle, node->sf_path,
	    bfp->l_start);
	if (error == ENOENT)
		sfnode_make_stale(node);

	mutex_exit(&sffs_lock);
	return (error);
}

/*ARGSUSED*/
static int
sffs_read(
	vnode_t		*vp,
	struct uio	*uio,
	int		ioflag,
	cred_t		*cred,
	caller_context_t *ct)
{
	sfnode_t	*node = VN2SFN(vp);
	int		error = 0;
	uint32_t	bytes;
	uint32_t	done;
	ulong_t 	offset;
	ssize_t		total;

	if (vp->v_type == VDIR)
		return (EISDIR);
	if (vp->v_type != VREG)
		return (EINVAL);
	if (uio->uio_loffset >= MAXOFFSET_T)
		return (0);
	if (uio->uio_loffset < 0)
		return (EINVAL);
	total = uio->uio_resid;
	if (total == 0)
		return (0);

	mutex_enter(&sffs_lock);
	if (node->sf_file == NULL) {
		ASSERT(node->sf_flag != ~0);
		sfnode_open(node, node->sf_flag);
		if (node->sf_file == NULL)
			return (EBADF);
	}

	do {
		offset = uio->uio_offset;
		done = bytes = MIN(PAGESIZE, uio->uio_resid);
		error = sfprov_read(node->sf_file, sffs_buffer, offset, &done);
		if (error == 0 && done > 0)
			error = uiomove(sffs_buffer, done, UIO_READ, uio);
	} while (error == 0 && uio->uio_resid > 0 && done > 0);

	mutex_exit(&sffs_lock);

	/*
	 * a partial read is never an error
	 */
	if (total != uio->uio_resid)
		error = 0;
	return (error);
}

/*ARGSUSED*/
static int
sffs_write(
	vnode_t		*vp,
	struct uio	*uiop,
	int		ioflag,
	cred_t		*cred,
	caller_context_t *ct)
{
	sfnode_t	*node = VN2SFN(vp);
	int		error = 0;
	uint32_t	bytes;
	uint32_t	done;
	ulong_t 	offset;
	ssize_t		total;
	rlim64_t	limit = uiop->uio_llimit;

	if (vp->v_type == VDIR)
		return (EISDIR);
	if (vp->v_type != VREG)
		return (EINVAL);

	/*
	 * We have to hold this lock for a long time to keep
	 * multiple FAPPEND writes from intermixing
	 */
	mutex_enter(&sffs_lock);
	if (node->sf_file == NULL) {
		ASSERT(node->sf_flag != ~0);
		sfnode_open(node, node->sf_flag);
		if (node->sf_file == NULL)
			return (EBADF);
	}

	sfnode_invalidate_stat_cache(node);

	if (ioflag & FAPPEND) {
		uint64_t endoffile;

		error = sfprov_get_size(node->sf_sffs->sf_handle,
		    node->sf_path, &endoffile);
		if (error == ENOENT)
			sfnode_make_stale(node);
		if (error != 0) {
			mutex_exit(&sffs_lock);
			return (error);
		}
		uiop->uio_loffset = endoffile;
	}

	if (vp->v_type != VREG || uiop->uio_loffset < 0) {
		mutex_exit(&sffs_lock);
		return (EINVAL);
	}
	if (limit == RLIM64_INFINITY || limit > MAXOFFSET_T)
		limit = MAXOFFSET_T;

	if (uiop->uio_loffset >= limit) {
		mutex_exit(&sffs_lock);
		return (EFBIG);
	}

	if (uiop->uio_loffset >= MAXOFFSET_T) {
		mutex_exit(&sffs_lock);
		return (EFBIG);
	}

	total = uiop->uio_resid;
	if (total == 0) {
		mutex_exit(&sffs_lock);
   		return (0);
	}

	do {
		offset = uiop->uio_offset;
		bytes = MIN(PAGESIZE, uiop->uio_resid);
		if (offset + bytes >= limit) {
			if (offset >= limit) {
				error = EFBIG;
				break;
			}
			bytes = limit - offset;
		}
		error = uiomove(sffs_buffer, bytes, UIO_WRITE, uiop);
		if (error != 0)
			break;
		done = bytes;
		if (error == 0)
			error = sfprov_write(node->sf_file, sffs_buffer,
			    offset, &done);
		total -= done;
		if (done != bytes) {
			uiop->uio_resid += bytes - done;
			break;
		}
	} while (error == 0 && uiop->uio_resid > 0 && done > 0);

	mutex_exit(&sffs_lock);

	/*
	 * A short write is never really an error.
	 */
	if (total != uiop->uio_resid)
		error = 0;
	return (error);
}

/*ARGSUSED*/
static int
sffs_access(
    vnode_t *vp,
    int mode,
    int flags,
    cred_t *cr
#if !defined(VBOX_VFS_SOLARIS_10U6)
    , caller_context_t *ct
#endif
    )
{
	sfnode_t *node = VN2SFN(vp);
	int error;

	mutex_enter(&sffs_lock);
	error = sfnode_access(node, mode, cr);
	mutex_exit(&sffs_lock);
	return (error);
}

/*
 * Lookup an entry in a directory and create a new vnode if found.
 */
/* ARGSUSED3 */
static int
sffs_lookup(
	vnode_t		*dvp,		/* the directory vnode */
	char		*name,		/* the name of the file or directory */
	vnode_t		**vpp,		/* the vnode we found or NULL */
	struct pathname	*pnp,
	int		flags,
	vnode_t		*rdir,
	cred_t		*cred
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct,
	int		*direntflags,
	struct pathname	*realpnp
#endif
	)
{
	int		error;
	sfnode_t	*node;

	/*
	 * dvp must be a directory
	 */
	if (dvp->v_type != VDIR)
		return (ENOTDIR);

	/*
	 * An empty component name or just "." means the directory itself.
	 * Don't do any further lookup or checking.
	 */
	if (strcmp(name, "") == 0 || strcmp(name, ".") == 0) {
		VN_HOLD(dvp);
		*vpp = dvp;
		return (0);
	}

	/*
	 * Check permission to look at this directory. We always allow "..".
	 */
	mutex_enter(&sffs_lock);
	if (strcmp(name, "..") != 0) {
		error = sfnode_access(VN2SFN(dvp), VEXEC, cred);
		if (error) {
			mutex_exit(&sffs_lock);
			return (error);
		}
	}

	/*
	 * Lookup the node.
	 */
	node = sfnode_lookup(VN2SFN(dvp), name, VNON, 0, NULL, 0, NULL);
	if (node != NULL)
		*vpp = sfnode_get_vnode(node);
	mutex_exit(&sffs_lock);
	return ((node == NULL) ? ENOENT : 0);
}

/*ARGSUSED*/
static int
sffs_create(
        vnode_t		*dvp,
        char		*name,
        struct vattr	*vap,
        vcexcl_t	exclusive,
        int		mode,
        vnode_t		**vpp,
        cred_t		*cr,
        int		flag
#if !defined(VBOX_VFS_SOLARIS_10U6)
        , caller_context_t *ct,
        vsecattr_t	*vsecp
#endif
        )
{
	vnode_t		*vp;
	sfnode_t	*node;
	int		error;

	ASSERT(name != NULL);

	/*
	 * this is used for regular files, not mkdir
	 */
	if (vap->va_type == VDIR)
		return (EISDIR);
	if (vap->va_type != VREG)
		return (EINVAL);

	/*
	 * is this a pre-existing file?
	 */
#if defined(VBOX_VFS_SOLARIS_10U6)
	error = sffs_lookup(dvp, name, &vp,
	    NULL, 0, NULL, cr);
#else
	error = sffs_lookup(dvp, name, &vp,
	    NULL, 0, NULL, cr, ct, NULL, NULL);
#endif
	if (error == ENOENT)
		vp = NULL;
	else if (error != 0)
		return (error);

	/*
	 * Operation on a pre-existing file.
	 */
	if (vp != NULL) {
		if (exclusive == EXCL) {
			VN_RELE(vp);
			return (EEXIST);
		}
		if (vp->v_type == VDIR && (mode & VWRITE) == VWRITE) {
			VN_RELE(vp);
			return (EISDIR);
		}

		mutex_enter(&sffs_lock);
		node = VN2SFN(vp);
		error = sfnode_access(node, mode, cr);
		if (error != 0) {
			mutex_exit(&sffs_lock);
			VN_RELE(vp);
			return (error);
		}

		sfnode_invalidate_stat_cache(VN2SFN(dvp));

		/*
		 * handle truncating an existing file
		 */
		if (vp->v_type == VREG && (vap->va_mask & AT_SIZE) &&
		    vap->va_size == 0) {
			sfnode_open(node, flag | FTRUNC);
			if (node->sf_path == NULL) {
				mutex_exit(&sffs_lock);
				VN_RELE(vp);
				return (ENOENT);
			}
		}
		mutex_exit(&sffs_lock);
		*vpp = vp;
		return (0);
	}

	/*
	 * Create a new node. First check for a race creating it.
	 */
	mutex_enter(&sffs_lock);
	node = sfnode_lookup(VN2SFN(dvp), name, VNON, 0, NULL, 0, NULL);
	if (node != NULL) {
		mutex_exit(&sffs_lock);
		return (EEXIST);
	}

	/*
	 * Doesn't exist yet and we have the lock, so create it.
	 */
	sfnode_invalidate_stat_cache(VN2SFN(dvp));
	int lookuperr;
	node = sfnode_lookup(VN2SFN(dvp), name, VREG,
		(vap->va_mask & AT_MODE) ? vap->va_mode : 0, NULL, 0, &lookuperr);

	if (node && node->sf_parent)
		sfnode_clear_dir_list(node->sf_parent);

	mutex_exit(&sffs_lock);
	if (node == NULL)
		return (lookuperr);
	*vpp = sfnode_get_vnode(node);
	return (0);
}

/*ARGSUSED*/
static int
sffs_mkdir(
	vnode_t		*dvp,
	char		*nm,
	vattr_t		*va,
	vnode_t		**vpp,
	cred_t		*cred
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct,
	int		flags,
	vsecattr_t	*vsecp
#endif
	)
{
	sfnode_t	*node;
	vnode_t		*vp;
	int		error;

	/*
	 * These should never happen
	 */
	ASSERT(nm != NULL);
	ASSERT(strcmp(nm, "") != 0);
	ASSERT(strcmp(nm, ".") != 0);
	ASSERT(strcmp(nm, "..") != 0);

	/*
	 * Do an unlocked look up first
	 */
#if defined(VBOX_VFS_SOLARIS_10U6)
	error = sffs_lookup(dvp, nm, &vp, NULL, 0, NULL, cred);
#else
	error = sffs_lookup(dvp, nm, &vp, NULL, 0, NULL, cred, ct, NULL, NULL);
#endif
	if (error == 0) {
		VN_RELE(vp);
		return (EEXIST);
	}
	if (error != ENOENT)
		return (error);

	/*
	 * Must be able to write in current directory
	 */
	mutex_enter(&sffs_lock);
	error = sfnode_access(VN2SFN(dvp), VWRITE, cred);
	if (error) {
		mutex_exit(&sffs_lock);
		return (error);
	}

	sfnode_invalidate_stat_cache(VN2SFN(dvp));
	int lookuperr = EACCES;
	node = sfnode_lookup(VN2SFN(dvp), nm, VDIR,
		(va->va_mode & AT_MODE) ? va->va_mode : 0, NULL, 0, &lookuperr);

	if (node && node->sf_parent)
		sfnode_clear_dir_list(node->sf_parent);

	mutex_exit(&sffs_lock);
	if (node == NULL)
		return (lookuperr);
	*vpp = sfnode_get_vnode(node);
	return (0);
}

/*ARGSUSED*/
static int
sffs_rmdir(
	struct vnode	*dvp,
	char		*nm,
	vnode_t		*cdir,
	cred_t		*cred
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct,
	int		flags
#endif
	)
{
	sfnode_t	 *node;
	vnode_t		*vp;
	int		error;

	/*
	 * Return error when removing . and ..
	 */
	if (strcmp(nm, ".") == 0 || strcmp(nm, "") == 0)
		return (EINVAL);
	if (strcmp(nm, "..") == 0)
		return (EEXIST);

#if defined(VBOX_VFS_SOLARIS_10U6)
	error = sffs_lookup(dvp, nm, &vp, NULL, 0, NULL, cred);
#else
	error = sffs_lookup(dvp, nm, &vp, NULL, 0, NULL, cred, ct, NULL, NULL);
#endif
	if (error)
		return (error);
	if (vp->v_type != VDIR) {
		VN_RELE(vp);
		return (ENOTDIR);
	}

#ifdef VBOXVFS_WITH_MMAP
	if (vn_vfswlock(vp)) {
		VN_RELE(vp);
		return (EBUSY);
	}
#endif

	if (vn_mountedvfs(vp)) {
		VN_RELE(vp);
		return (EBUSY);
	}

	node = VN2SFN(vp);

	mutex_enter(&sffs_lock);
	error = sfnode_access(VN2SFN(dvp), VEXEC | VWRITE, cred);
	if (error)
		goto done;

	/*
	 * If anything else is using this vnode, then fail the remove.
	 * Why?  Windows hosts can't remove something that is open,
	 * so we have to sfprov_close() it first.
	 * There is no errno for this - since it's not a problem on UNIX,
	 * but EINVAL is the closest.
	 */
	if (node->sf_file != NULL) {
		if (vp->v_count > 1) {
			error = EINVAL;
			goto done;
		}
		(void)sfprov_close(node->sf_file);
		node->sf_file = NULL;
	}

	/*
	 * Remove the directory on the host and mark the node as stale.
	 */
	sfnode_invalidate_stat_cache(VN2SFN(dvp));
	error = sfprov_rmdir(node->sf_sffs->sf_handle, node->sf_path);
	if (error == ENOENT || error == 0)
		sfnode_make_stale(node);

	if (node->sf_parent)
		sfnode_clear_dir_list(node->sf_parent);
done:
	mutex_exit(&sffs_lock);
#ifdef VBOXVFS_WITH_MMAP
	vn_vfsunlock(vp);
#endif
	VN_RELE(vp);
	return (error);
}


#ifdef VBOXVFS_WITH_MMAP
static caddr_t
sffs_page_map(
	page_t *ppage,
	enum seg_rw segaccess)
{
	/* Use seg_kpm driver if possible (64-bit) */
	if (kpm_enable)
		return (hat_kpm_mapin(ppage, NULL));
	ASSERT(segaccess == S_READ || segaccess == S_WRITE);
	return (ppmapin(ppage, PROT_READ | ((segaccess == S_WRITE) ? PROT_WRITE : 0), (caddr_t)-1));
}


static void
sffs_page_unmap(
	page_t *ppage,
	caddr_t addr)
{
	if (kpm_enable)
		hat_kpm_mapout(ppage, NULL, addr);
	else
		ppmapout(addr);
}


/*
 * Called when there's no page in the cache. This will create new page(s) and read
 * the file data into it.
 */
static int
sffs_readpages(
	vnode_t		*dvp,
	offset_t	off,
	page_t		*pagelist[],
	size_t		pagelistsize,
	struct seg  *segp,
	caddr_t		addr,
	enum seg_rw	segaccess)
{
	ASSERT(MUTEX_HELD(&sffs_lock));

	int error = 0;
	u_offset_t io_off, total;
	size_t io_len;
	page_t *ppages;
	page_t *pcur;

	sfnode_t *node = VN2SFN(dvp);
	ASSERT(node);
	ASSERT(node->sf_file);

	if (pagelistsize == PAGESIZE)
	{
		io_off = off;
		io_len = PAGESIZE;
		ppages = page_create_va(dvp, io_off, io_len, PG_WAIT | PG_EXCL, segp, addr);
	}
	else
		ppages = pvn_read_kluster(dvp, off, segp, addr, &io_off, &io_len, off, pagelistsize, 0);

	/* If page already exists return success */
	if (!ppages)
	{
		*pagelist = NULL;
		return (0);
	}

	/*
	 * Map & read page-by-page.
	 */
	total = io_off + io_len;
	pcur = ppages;
	while (io_off < total)
	{
		ASSERT3U(io_off, ==, pcur->p_offset);

		caddr_t virtaddr = sffs_page_map(pcur, segaccess);
		uint32_t bytes = PAGESIZE;
		error = sfprov_read(node->sf_file, virtaddr, io_off, &bytes);
		/*
		 * If we reuse pages without zero'ing them, one process can mmap() and read-past the length
		 * to read previously mmap'd contents (from possibly other processes).
		 */
		if (error == 0 && bytes < PAGESIZE)
			memset(virtaddr + bytes, 0, PAGESIZE - bytes);
		sffs_page_unmap(pcur, virtaddr);
		if (error != 0)
		{
			cmn_err(CE_WARN, "sffs_readpages: sfprov_read() failed. error=%d bytes=%u\n", error, bytes);
			/* Get rid of all kluster pages read & bail.  */
			pvn_read_done(ppages, B_ERROR);
			return (error);
		}
		pcur = pcur->p_next;
		io_off += PAGESIZE;
	}

	/*
	 * Fill in the pagelist from kluster at the requested offset.
	 */
	pvn_plist_init(ppages, pagelist, pagelistsize, off, io_len, segaccess);
	ASSERT(pagelist == NULL || (*pagelist)->p_offset == off);
	return (0);
}


/*ARGSUSED*/
static int
sffs_getpage(
	vnode_t		*dvp,
	offset_t	off,
	size_t		len,
	uint_t 		*protp,
	page_t 		*pagelist[],
	size_t		pagelistsize,
	struct seg	*segp,
	caddr_t 	addr,
	enum seg_rw	segaccess,
	cred_t		*credp
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
	int error = 0;
	int is_recursive = 0;
	page_t **pageliststart = pagelist;
	sfnode_t *node = VN2SFN(dvp);
	ASSERT(node);
	ASSERT(node->sf_file);

	if (segaccess == S_WRITE)
		return (ENOSYS);	/* Will this ever happen? */

	/* Don't bother about faultahead for now. */
	if (pagelist == NULL)
		return (0);

	if (len > pagelistsize)
		len = pagelistsize;
	else
		len = P2ROUNDUP(len, PAGESIZE);
	ASSERT(pagelistsize >= len);

	if (protp)
		*protp = PROT_ALL;

	/*
	 * The buffer passed to sffs_write may be mmap'd so we may get a
	 * pagefault there, in which case we'll end up here with this thread
	 * already owning the mutex. Mutexes aren't recursive.
	 */
	if (mutex_owner(&sffs_lock) == curthread)
		is_recursive = 1;
	else
		mutex_enter(&sffs_lock);

	/* Don't map pages past end of the file. */
	if (off + len > node->sf_stat.sf_size + PAGEOFFSET)
	{
		if (!is_recursive)
			mutex_exit(&sffs_lock);
		return (EFAULT);
	}

	while (len > 0)
	{
		/*
		 * Look for pages in the requested offset range, or create them if we can't find any.
		 */
		if ((*pagelist = page_lookup(dvp, off, SE_SHARED)) != NULL)
			*(pagelist + 1) = NULL;
		else if ((error = sffs_readpages(dvp, off, pagelist, pagelistsize, segp, addr, segaccess)) != 0)
		{
			while (pagelist > pageliststart)
				page_unlock(*--pagelist);

			*pagelist = NULL;
			if (!is_recursive)
				mutex_exit(&sffs_lock);
			return (error);
		}

		while (*pagelist)
		{
			ASSERT3U((*pagelist)->p_offset, ==, off);
			off += PAGESIZE;
			addr += PAGESIZE;
			if (len > 0)
			{
				ASSERT3U(len, >=, PAGESIZE);
				len -= PAGESIZE;
			}

			ASSERT3U(pagelistsize,  >=, PAGESIZE);
			pagelistsize -= PAGESIZE;
			pagelist++;
		}
	}

	/*
	 * Fill the page list array with any pages left in the cache.
	 */
	while (   pagelistsize > 0
		   && (*pagelist++ = page_lookup_nowait(dvp, off, SE_SHARED)))
	{
		off += PAGESIZE;
		pagelistsize -= PAGESIZE;
	}

	*pagelist = NULL;
	if (!is_recursive)
		mutex_exit(&sffs_lock);
	return (error);
}


/*ARGSUSED*/
static int
sffs_putpage(
	vnode_t		*dvp,
	offset_t	off,
	size_t		len,
	int			flags,
	cred_t		*credp
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
	/*
	 * We don't support PROT_WRITE mmaps.
	 */
	return (ENOSYS);
}


/*ARGSUSED*/
static int
sffs_discardpage(
	vnode_t		*dvp,
	page_t		*ppage,
	u_offset_t	*poff,
	size_t		*plen,
	int			flags,
	cred_t		*pcred)
{
	/*
	 * This would not get invoked i.e. via pvn_vplist_dirty() since we don't support
	 * PROT_WRITE mmaps and therefore will not have dirty pages.
	 */
	pvn_write_done(ppage, B_INVAL | B_ERROR | B_FORCE);
	return (0);
}


/*ARGSUSED*/
static int
sffs_map(
	vnode_t		*dvp,
	offset_t	off,
	struct as 	*asp,
	caddr_t		*addrp,
	size_t		len,
	uchar_t		prot,
	uchar_t		maxprot,
	uint_t		flags,
	cred_t 		*credp
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
	/*
	 * Invocation: mmap()->smmap_common()->VOP_MAP()->sffs_map(). Once the
	 * segment driver creates the new segment via segvn_create(), it'll
	 * invoke down the line VOP_ADDMAP()->sffs_addmap()
	 */
	int error = 0;
	sfnode_t *node = VN2SFN(dvp);
	ASSERT(node);
	if ((flags & MAP_SHARED) && (prot & PROT_WRITE))
		return (ENOTSUP);

	if (off < 0 || len > MAXOFFSET_T - off)
		return (ENXIO);

	if (dvp->v_type != VREG)
		return (ENODEV);

	if (dvp->v_flag & VNOMAP)
		return (ENOSYS);

	if (vn_has_mandatory_locks(dvp, node->sf_stat.sf_mode))
		return (EAGAIN);

	mutex_enter(&sffs_lock);
	as_rangelock(asp);

#if defined(VBOX_VFS_SOLARIS_10U6)
	if ((flags & MAP_FIXED) == 0)
	{
        if (g_fVBoxVFS_SolOldAddrMap)
            g_VBoxVFS_SolAddrMap.MapAddr.pfnSol_map_addr_old(addrp, len, off, 1, flags);
        else
            g_VBoxVFS_SolAddrMap.MapAddr.pfnSol_map_addr(addrp, len, off, flags);
		if (*addrp == NULL)
			error = ENOMEM;
	}
	else
		as_unmap(asp, *addrp, len);	/* User specified address, remove any previous mappings */
#else
    if (g_fVBoxVFS_SolOldAddrMap)
	    error = g_VBoxVFS_SolAddrMap.ChooseAddr.pfnSol_choose_addr_old(asp, addrp, len, off, 1, flags);
    else
	    error = g_VBoxVFS_SolAddrMap.ChooseAddr.pfnSol_choose_addr(asp, addrp, len, off, flags);
#endif

	if (error)
	{
		as_rangeunlock(asp);
		mutex_exit(&sffs_lock);
		return (error);
	}

	segvn_crargs_t vnodeargs;
	memset(&vnodeargs, 0, sizeof(vnodeargs));
	vnodeargs.vp = dvp;
	vnodeargs.cred = credp;
	vnodeargs.offset = off;
	vnodeargs.type = flags & MAP_TYPE;
	vnodeargs.prot = prot;
	vnodeargs.maxprot = maxprot;
	vnodeargs.flags = flags & ~MAP_TYPE;
	vnodeargs.amp = NULL;		/* anon. mapping */
	vnodeargs.szc = 0;			/* preferred page size code */
	vnodeargs.lgrp_mem_policy_flags = 0;

	error = as_map(asp, *addrp, len, segvn_create, &vnodeargs);

	as_rangeunlock(asp);
	mutex_exit(&sffs_lock);
	return (error);
}


/*ARGSUSED*/
static int
sffs_addmap(
	vnode_t		*dvp,
	offset_t	off,
	struct as	*asp,
	caddr_t		addr,
	size_t		len,
	uchar_t		prot,
	uchar_t		maxprot,
	uint_t		flags,
	cred_t		*credp
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
	if (dvp->v_flag & VNOMAP)
		return (ENOSYS);
	return (0);
}


/*ARGSUSED*/
static int
sffs_delmap(
	vnode_t		*dvp,
	offset_t	off,
	struct as	*asp,
	caddr_t		addr,
	size_t		len,
	uint_t		prot,
	uint_t		maxprot,
	uint_t		flags,
	cred_t		*credp
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
	if (dvp->v_flag & VNOMAP)
		return (ENOSYS);

	return (0);
}
#endif /* VBOXVFS_WITH_MMAP */


/*ARGSUSED*/
static int
sffs_readlink(
	vnode_t		*vp,
	uio_t		*uiop,
	cred_t		*cred
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
	sfnode_t	*node;
	int			error = 0;
	char		*target = NULL;

	if (uiop->uio_iovcnt != 1)
		return (EINVAL);

	if (vp->v_type != VLNK)
		return (EINVAL);

	mutex_enter(&sffs_lock);
	node = VN2SFN(vp);

	target = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	error = sfprov_readlink(node->sf_sffs->sf_handle, node->sf_path, target,
	    MAXPATHLEN);
	if (error)
		goto done;

	error = uiomove(target, strlen(target), UIO_READ, uiop);

done:
	mutex_exit(&sffs_lock);
	if (target)
		kmem_free(target, MAXPATHLEN);
	return (error);
}


/*ARGSUSED*/
static int
sffs_symlink(
	vnode_t		*dvp,
	char		*linkname,
	vattr_t		*vap,
	char		*target,
	cred_t		*cred
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct,
	int		flags
#endif
	)
{
	sfnode_t	*dir;
	sfnode_t	*node;
	sffs_stat_t  stat;
	int			 error = 0;
	char		*fullpath;

	/*
	 * These should never happen
	 */
	ASSERT(linkname != NULL);
	ASSERT(strcmp(linkname, "") != 0);
	ASSERT(strcmp(linkname, ".") != 0);
	ASSERT(strcmp(linkname, "..") != 0);

	/*
	 * Basic checks.
	 */
	if (vap->va_type != VLNK)
		return (EINVAL);

	mutex_enter(&sffs_lock);

	if (sfnode_lookup(VN2SFN(dvp), linkname, VNON, 0, NULL, 0, NULL) !=
		NULL) {
		error = EEXIST;
		goto done;
	}

	dir = VN2SFN(dvp);
	error = sfnode_access(dir, VWRITE, cred);
	if (error)
		goto done;

	/*
	 * Create symlink. Note that we ignore vap->va_mode because generally
	 * we can't change the attributes of the symlink itself.
	 */
	fullpath = sfnode_construct_path(dir, linkname);
	error = sfprov_symlink(dir->sf_sffs->sf_handle, fullpath, target,
	    &stat);
	kmem_free(fullpath, strlen(fullpath) + 1);
	if (error)
		goto done;

	node = sfnode_lookup(dir, linkname, VLNK, 0, &stat,
		sfnode_cur_time_usec(), NULL);

	sfnode_invalidate_stat_cache(dir);
	sfnode_clear_dir_list(dir);

done:
	mutex_exit(&sffs_lock);
	return (error);
}


/*ARGSUSED*/
static int
sffs_remove(
	vnode_t		*dvp,
	char		*name,
	cred_t		*cred
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct,
	int		flags
#endif
	)
{
	vnode_t		*vp;
	sfnode_t	*node;
	int		error;

	/*
	 * These should never happen
	 */
	ASSERT(name != NULL);
	ASSERT(strcmp(name, "..") != 0);

#if defined(VBOX_VFS_SOLARIS_10U6)
	error = sffs_lookup(dvp, name, &vp,
	    NULL, 0, NULL, cred);
#else
	error = sffs_lookup(dvp, name, &vp,
	    NULL, 0, NULL, cred, ct, NULL, NULL);
#endif
	if (error)
		return (error);
	node = VN2SFN(vp);

	mutex_enter(&sffs_lock);
	error = sfnode_access(VN2SFN(dvp), VEXEC | VWRITE, cred);
	if (error)
		goto done;

	/*
	 * If anything else is using this vnode, then fail the remove.
	 * Why?  Windows hosts can't sfprov_remove() a file that is open,
	 * so we have to sfprov_close() it first.
	 * There is no errno for this - since it's not a problem on UNIX,
	 * but ETXTBSY is the closest.
	 */
	if (node->sf_file != NULL) {
		if (vp->v_count > 1) {
			error = ETXTBSY;
			goto done;
		}
		(void)sfprov_close(node->sf_file);
		node->sf_file = NULL;
	}

	/*
	 * Remove the file on the host and mark the node as stale.
	 */
	sfnode_invalidate_stat_cache(VN2SFN(dvp));

	error = sfprov_remove(node->sf_sffs->sf_handle, node->sf_path,
		node->sf_type == VLNK);
	if (error == ENOENT || error == 0)
		sfnode_make_stale(node);

	if (node->sf_parent)
		sfnode_clear_dir_list(node->sf_parent);
done:
	mutex_exit(&sffs_lock);
	VN_RELE(vp);
	return (error);
}

/*ARGSUSED*/
static int
sffs_rename(
	vnode_t		*old_dir,
	char		*old_nm,
	vnode_t		*new_dir,
	char		*new_nm,
	cred_t		*cred
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct,
	int		flags
#endif
	)
{
	char		*newpath;
	int		error;
	sfnode_t	*node;

	if (strcmp(new_nm, "") == 0 ||
	    strcmp(new_nm, ".") == 0 ||
	    strcmp(new_nm, "..") == 0 ||
	    strcmp(old_nm, "") == 0 ||
	    strcmp(old_nm, ".") == 0 ||
	    strcmp(old_nm, "..") == 0)
		return (EINVAL);

	/*
	 * make sure we have permission to do the rename
	 */
	mutex_enter(&sffs_lock);
	error = sfnode_access(VN2SFN(old_dir), VEXEC | VWRITE, cred);
	if (error == 0 && new_dir != old_dir)
		error = sfnode_access(VN2SFN(new_dir), VEXEC | VWRITE, cred);
	if (error)
		goto done;

	node = sfnode_lookup(VN2SFN(old_dir), old_nm, VNON, 0, NULL, 0, NULL);
	if (node == NULL) {
		error = ENOENT;
		goto done;
	}

	/*
	 * Rename the file on the host and in our caches.
	 */
	sfnode_invalidate_stat_cache(node);
	sfnode_invalidate_stat_cache(VN2SFN(old_dir));
	sfnode_invalidate_stat_cache(VN2SFN(new_dir));

	newpath = sfnode_construct_path(VN2SFN(new_dir), new_nm);
	error = sfprov_rename(node->sf_sffs->sf_handle, node->sf_path, newpath,
	    node->sf_type == VDIR);
	if (error == 0)
		sfnode_rename(node, VN2SFN(new_dir), newpath);
	else {
		kmem_free(newpath, strlen(newpath) + 1);
		if (error == ENOENT)
			sfnode_make_stale(node);
	}
done:
	mutex_exit(&sffs_lock);
	return (error);
}


/*ARGSUSED*/
static int
sffs_fsync(
    vnode_t *vp,
    int flag,
    cred_t *cr
#if !defined(VBOX_VFS_SOLARIS_10U6)
    , caller_context_t *ct
#endif
    )
{
	sfnode_t *node;
	int		error;

	/*
	 * Ask the host to sync any data it may have cached for open files.
	 */
	mutex_enter(&sffs_lock);
	node = VN2SFN(vp);
	if (node->sf_file == NULL)
		error = EBADF;
	else if (node->sf_sffs->sf_fsync)
		error = sfprov_fsync(node->sf_file);
	else
		error = 0;
	mutex_exit(&sffs_lock);
	return (error);
}

/*
 * This may be the last reference, possibly time to close the file and
 * destroy the vnode. If the sfnode is stale, we'll destroy that too.
 */
/*ARGSUSED*/
static void
sffs_inactive(
    vnode_t *vp,
    cred_t *cr
#if !defined(VBOX_VFS_SOLARIS_10U6)
    , caller_context_t *ct
#endif
    )
{
	sfnode_t *node;

	/*
	 * nothing to do if this isn't the last use
	 */
	mutex_enter(&sffs_lock);
	node = VN2SFN(vp);
	mutex_enter(&vp->v_lock);
	if (vp->v_count > 1) {
		--vp->v_count;
		mutex_exit(&vp->v_lock);
		mutex_exit(&sffs_lock);
		return;
	}

	if (vn_has_cached_data(vp)) {
#ifdef VBOXVFS_WITH_MMAP
		/* We're fine with releasing the vnode lock here as we should be covered by the sffs_lock */
		mutex_exit(&vp->v_lock);
		/* We won't have any dirty pages, this will just invalidate (destroy) the pages and move it to the cachelist. */
		pvn_vplist_dirty(vp, 0 /* offset */, sffs_discardpage, B_INVAL, cr);
		mutex_enter(&vp->v_lock);
#else
		panic("sffs_inactive() found cached data");
#endif
	}

	/*
	 * destroy the vnode
	 */
	node->sf_vnode = NULL;
	mutex_exit(&vp->v_lock);
	vn_invalid(vp);
	vn_free(vp);
	LogFlowFunc(("  %s vnode cleared\n", node->sf_path));

	/*
	 * Close the sf_file for the node.
	 */
	if (node->sf_file != NULL) {
		(void)sfprov_close(node->sf_file);
		node->sf_file = NULL;
	}

	/*
	 * Free the directory entries for the node. This should normally
	 * have been taken care of in sffs_close(), but better safe than
	 * sorry.
	 */
	sfnode_clear_dir_list(node);

	/*
	 * If the node is stale, we can also destroy it.
	 */
	if (node->sf_is_stale && node->sf_children == 0)
		sfnode_destroy(node);

	mutex_exit(&sffs_lock);
	return;
}

/*
 * All the work for this is really done in sffs_lookup().
 */
/*ARGSUSED*/
static int
sffs_open(
    vnode_t **vpp,
    int flag,
    cred_t *cr
#if !defined(VBOX_VFS_SOLARIS_10U6)
    , caller_context_t *ct
#endif
    )
{
	sfnode_t *node;
	int	error = 0;

	mutex_enter(&sffs_lock);

	node = VN2SFN(*vpp);
	sfnode_open(node, flag);
	if (node->sf_file == NULL)
		error = EINVAL;
	mutex_exit(&sffs_lock);

	return (error);
}

/*
 * All the work for this is really done in inactive.
 */
/*ARGSUSED*/
static int
sffs_close(
	vnode_t *vp,
	int flag,
	int count,
	offset_t offset,
	cred_t *cr
#if !defined(VBOX_VFS_SOLARIS_10U6)
	, caller_context_t *ct
#endif
	)
{
	sfnode_t *node;

	mutex_enter(&sffs_lock);
	node = VN2SFN(vp);

	/*
	 * Free the directory entries for the node. We do this on this call
	 * here because the directory node may not become inactive for a long
	 * time after the readdir is over. Case in point, if somebody cd's into
	 * the directory then it won't become inactive until they cd away again.
	 * In such a case we would end up with the directory listing not getting
	 * updated (i.e. the result of 'ls' always being the same) until they
	 * change the working directory.
	 */
	sfnode_clear_dir_list(node);

	sfnode_invalidate_stat_cache(node);

	if (node->sf_file != NULL && vp->v_count <= 1)
	{
		(void)sfprov_close(node->sf_file);
		node->sf_file = NULL;
	}

	mutex_exit(&sffs_lock);
	return (0);
}

/* ARGSUSED */
static int
sffs_seek(
    vnode_t *v,
    offset_t o,
    offset_t *no
#if !defined(VBOX_VFS_SOLARIS_10U6)
    , caller_context_t *ct
#endif
    )
{
	if (*no < 0 || *no > MAXOFFSET_T)
		return (EINVAL);

	if (v->v_type == VDIR)
	{
		sffs_dirents_t *cur_buf = VN2SFN(v)->sf_dir_list;
		off_t offset = 0;

		if (cur_buf == NULL)
			return (0);

		while (cur_buf != NULL) {
			if (*no >= offset && *no <= offset + cur_buf->sf_len)
				return (0);
			offset += cur_buf->sf_len;
			cur_buf = cur_buf->sf_next;
		}
		return (EINVAL);
	}
	return (0);
}



/*
 * By returning an error for this, we prevent anything in sffs from
 * being re-exported by NFS
 */
/* ARGSUSED */
static int
sffs_fid(
    vnode_t *vp,
    fid_t *fidp
#if !defined(VBOX_VFS_SOLARIS_10U6)
    , caller_context_t *ct
#endif
    )
{
	return (ENOTSUP);
}

/*
 * vnode operations for regular files
 */
const fs_operation_def_t sffs_ops_template[] = {
	VOPNAME_ACCESS,		{ .vop_access = sffs_access },
	VOPNAME_CLOSE,		{ .vop_close = sffs_close },
	VOPNAME_CREATE,		{ .vop_create = sffs_create },
	VOPNAME_FID,		{ .vop_fid = sffs_fid },
	VOPNAME_FSYNC,		{ .vop_fsync = sffs_fsync },
	VOPNAME_GETATTR,	{ .vop_getattr = sffs_getattr },
	VOPNAME_INACTIVE,	{ .vop_inactive = sffs_inactive },
	VOPNAME_LOOKUP,		{ .vop_lookup = sffs_lookup },
	VOPNAME_MKDIR,		{ .vop_mkdir = sffs_mkdir },
	VOPNAME_OPEN,		{ .vop_open = sffs_open },
	VOPNAME_PATHCONF,	{ .vop_pathconf = sffs_pathconf },
	VOPNAME_READ,		{ .vop_read = sffs_read },
	VOPNAME_READDIR,	{ .vop_readdir = sffs_readdir },
	VOPNAME_READLINK,	{ .vop_readlink = sffs_readlink },
	VOPNAME_REMOVE,		{ .vop_remove = sffs_remove },
	VOPNAME_RENAME,		{ .vop_rename = sffs_rename },
	VOPNAME_RMDIR,		{ .vop_rmdir = sffs_rmdir },
	VOPNAME_SEEK,		{ .vop_seek = sffs_seek },
	VOPNAME_SETATTR,	{ .vop_setattr = sffs_setattr },
	VOPNAME_SPACE,		{ .vop_space = sffs_space },
	VOPNAME_SYMLINK,	{ .vop_symlink = sffs_symlink },
	VOPNAME_WRITE,		{ .vop_write = sffs_write },

# ifdef VBOXVFS_WITH_MMAP
	VOPNAME_MAP,		{ .vop_map = sffs_map },
	VOPNAME_ADDMAP,		{ .vop_addmap = sffs_addmap },
	VOPNAME_DELMAP,		{ .vop_delmap = sffs_delmap },
	VOPNAME_GETPAGE,	{ .vop_getpage = sffs_getpage },
	VOPNAME_PUTPAGE,	{ .vop_putpage = sffs_putpage },
# endif

	NULL,			NULL
};

/*
 * Also, init and fini functions...
 */
int
sffs_vnode_init(void)
{
	int err;

	err = vn_make_ops("sffs", sffs_ops_template, &sffs_ops);
	if (err)
		return (err);

	avl_create(&sfnodes, sfnode_compare, sizeof (sfnode_t),
	    offsetof(sfnode_t, sf_linkage));
	avl_create(&stale_sfnodes, sfnode_compare, sizeof (sfnode_t),
	    offsetof(sfnode_t, sf_linkage));

	sffs_buffer = kmem_alloc(PAGESIZE, KM_SLEEP);

	return (0);
}

void
sffs_vnode_fini(void)
{
	if (sffs_ops)
		vn_freevnodeops(sffs_ops);
	ASSERT(avl_first(&sfnodes) == NULL);
	avl_destroy(&sfnodes);
	if (sffs_buffer != NULL) {
		kmem_free(sffs_buffer, PAGESIZE);
		sffs_buffer = NULL;
	}
}

/*
 * Utility at unmount to get all nodes in that mounted filesystem removed.
 */
int
sffs_purge(struct sffs_data *sffs)
{
	sfnode_t *node;
	sfnode_t *prev;

	/*
	 * Check that no vnodes are active.
	 */
	if (sffs->sf_rootnode->v_count > 1)
		return (-1);
	for (node = avl_first(&sfnodes); node;
	    node = AVL_NEXT(&sfnodes, node)) {
		if (node->sf_sffs == sffs && node->sf_vnode &&
		    node->sf_vnode != sffs->sf_rootnode)
			return (-1);
	}
	for (node = avl_first(&stale_sfnodes); node;
	    node = AVL_NEXT(&stale_sfnodes, node)) {
		if (node->sf_sffs == sffs && node->sf_vnode &&
		    node->sf_vnode != sffs->sf_rootnode)
			return (-1);
	}

	/*
	 * All clear to destroy all node information. Since there are no
	 * vnodes, the make stale will cause deletion.
	 */
	VN_RELE(sffs->sf_rootnode);
	mutex_enter(&sffs_lock);
	for (prev = NULL;;) {
		if (prev == NULL)
			node = avl_first(&sfnodes);
		else
			node = AVL_NEXT(&sfnodes, prev);

		if (node == NULL)
			break;

		if (node->sf_sffs == sffs) {
			if (node->sf_vnode != NULL)
				panic("vboxfs: purge hit active vnode");
			sfnode_make_stale(node);
		} else {
			prev = node;
		}
	}
	mutex_exit(&sffs_lock);
	return (0);
}

#if 0
/* Debug helper functions */
static void
sfnode_print(sfnode_t *node)
{
	Log(("0x%p", node));
	Log((" type=%s (%d)",
		node->sf_type == VDIR ? "VDIR" :
		node->sf_type == VNON ? "VNON" :
		node->sf_type == VLNK ? "VLNK" :
		node->sf_type == VREG ? "VREG" : "other", node->sf_type));
	Log((" ino=%d", (uint_t)node->sf_ino));
	Log((" path=%s", node->sf_path));
	Log((" parent=0x%p", node->sf_parent));
	if (node->sf_children)
		Log((" children=%d", node->sf_children));
	if (node->sf_vnode)
		Log((" vnode=0x%p", node->sf_vnode));
	Log(("%s\n", node->sf_is_stale ? " STALE" : ""));
}

static void
sfnode_list(void)
{
	sfnode_t *n;
	for (n = avl_first(&sfnodes); n != NULL; n = AVL_NEXT(&sfnodes, n))
		sfnode_print(n);
	for (n = avl_first(&stale_sfnodes); n != NULL;
	    n = AVL_NEXT(&stale_sfnodes, n))
		sfnode_print(n);
}
#endif

