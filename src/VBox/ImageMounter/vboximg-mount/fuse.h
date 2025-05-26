/* $Id: fuse.h 169001 2025-05-26 19:52:08Z klaus.espenlaub@oracle.com $ */
/** @file
 * Module to dynamically load libfuse/libosxfuse and load all symbols
 * which are needed by vboximg-mount.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
 * --------------------------------------------------------------------
 *
 * This code is based on and contains parts of:
 *
 * libfuse
 *
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB.
 */

#ifndef VBOX_INCLUDED_SRC_vboximg_mount_fuse_h
#define VBOX_INCLUDED_SRC_vboximg_mount_fuse_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/stdarg.h>

RT_C_DECLS_BEGIN

/* Forward declaration of stat buffer. */
struct stat;

/**
 * Fuse option structure.
 */
typedef struct fuse_opt
{
    /** Argument template with optional parameter formatting. */
    const char      *pszTempl;
    /** Offset where the parameter is stored inside the data struct passed to fuse_opt_parse(). */
    unsigned long   uOffset;
    /** The value to set the variable to if the template has no argument format. */
    int             uVal;
} fuse_opt;

#define FUSE_OPT_KEY(templ, key) { templ, -1U, key }
#define FUSE_OPT_END { NULL, 0, 0 }
#define FUSE_OPT_KEY_NONOPT  -2

/**
 * Fuse argument vector.
 */
typedef struct fuse_args
{
    int argc;
    char **argv;
    int allocated;
} fuse_args;

#define FUSE_ARGS_INIT(argc, argv) { argc, argv, 0 }


/**
 * Fuse file info structure - for us only the fh member is of interest for now.
 */
typedef struct fuse_file_info
{
    int           flags;
    unsigned long fh_old;
    int           writepage;
    unsigned int  oth_flags;
    uint64_t      fh;
    uint64_t      lock_owner;
} fuse_file_info;

/** Option processing function. */
typedef int (*fuse_opt_proc_t)(void *data, const char *arg, int key, struct fuse_args *outargs);

/** Directory entry filler function. */
typedef int (*fuse_fill_dir_t) (void *buf, const char *name, const struct stat *stbuf, off_t off);


/**
 * Fuse FS callback table implementing the filesystem functionality.
 *
 * Only required methods are filled out, others are stubbed (change as required).
 */
typedef struct fuse_operations
{
    int (*getattr) (const char *pszPath, struct stat *pStatBuf);
    int (*readlink) (const char *pszPath, char *pbBuf, size_t cbBuf);
    PFNRT getdir;
    PFNRT mknod;
    PFNRT mkdir;
    PFNRT unlink;
    PFNRT rmdir;
    PFNRT symlink;
    PFNRT rename;
    PFNRT link;
    PFNRT chmod;
    PFNRT chown;
    PFNRT truncate;
    PFNRT utime;
    int (*open) (const char *pszPath, struct fuse_file_info *pInfo);
    int (*read) (const char *pszPath, char *pbBuf, size_t cbBuf, off_t off, struct fuse_file_info *pInfo);
    int (*write) (const char *pszPath, const char *pbBuf, size_t cbBuf, off_t off, struct fuse_file_info *pInfo);
    PFNRT statfs;
    PFNRT flush;
    int (*release) (const char *pszPath, struct fuse_file_info *pInfo);
    PFNRT fsync;
    PFNRT setxattr; /* OSXFuse has a different parameter layout. */
    PFNRT getxattr; /* OSXFuse has a different parameter layout. */
    PFNRT listxattr;
    PFNRT removexattr;
    int (*opendir) (const char *pszPath, struct fuse_file_info *pInfo);
    int (*readdir) (const char *pszPath, void *pvBuf, fuse_fill_dir_t pfnFiller, off_t offset, struct fuse_file_info *pInfo);
    PFNRT releasedir;
    PFNRT fsyncdir;
    PFNRT init;
    PFNRT destroy;
    PFNRT access;
    PFNRT create;
    PFNRT ftruncate;
    PFNRT fgettatr;
    PFNRT lock;
    PFNRT utimens;
    PFNRT bmap;
    unsigned int flag_null_path_ok: 1;
    unsigned int flag_nopath: 1;
    unsigned int flag_utime_omit_ok: 1;
    unsigned int flag_reserved: 20;
    PFNRT ioctl;
    PFNRT poll;
    PFNRT write_buf;
    PFNRT read_buf;
    PFNRT flock;
    PFNRT fallocate;
#ifdef RT_OS_DARWIN
    PFNRT rsvd00;
    PFNRT rsvd01;
    PFNRT rsvd02;
    PFNRT statfs_x;
    PFNRT setvolname;
    PFNRT exchange;
    PFNRT getxtimes;
    PFNRT setbkuptime;
    PFNRT setchgtime;
    PFNRT setcrtime;
    PFNRT chflags;
    PFNRT setattr_x;
    PFNRT fsetattr_x;
#endif
} fuse_operations;

/* Declarations of the functions that we need from libfuse */
#define VBOX_FUSE_GENERATE_HEADER

#include "fuse-calls.h"

#undef VBOX_FUSE_GENERATE_HEADER

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_vboximg_mount_fuse_h */

