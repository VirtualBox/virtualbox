/* $Id: fuse-calls.h 169001 2025-05-26 19:52:08Z klaus.espenlaub@oracle.com $ */
/** @file
 * Stubs for dynamically loading libfuse/libosxfuse and the symbols which are needed by
 * VirtualBox.
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
 */
/** The file name of the fuse library */
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_SOLARIS)
# define RT_RUNTIME_LOADER_LIB_NAME  "libfuse.so.2"
#elif defined(RT_OS_DARWIN)
# define RT_RUNTIME_LOADER_LIB_NAME  "libosxfuse.dylib"
#else
# error "Unsupported host OS, manual work required"
#endif

/** The name of the loader function */
#define RT_RUNTIME_LOADER_FUNCTION  RTFuseLoadLib

/** The following are the symbols which we need from the fuse library. */
#define RT_RUNTIME_LOADER_INSERT_SYMBOLS \
 RT_PROXY_STUB(fuse_main_real, int, (int argc, char **argv, struct fuse_operations *fuse_ops, size_t op_size, void *pv), \
               (argc, argv, fuse_ops, op_size, pv)) \
 RT_PROXY_STUB(fuse_opt_parse, int, (struct fuse_args *args, void *data, const struct fuse_opt opts[], fuse_opt_proc_t proc), \
               (args, data, opts, proc)) \
 RT_PROXY_STUB(fuse_opt_add_arg, int, (struct fuse_args *args, const char *arg), \
               (args, arg)) \
 RT_PROXY_STUB(fuse_opt_free_args, void, (struct fuse_args *args), \
               (args))

#ifdef VBOX_FUSE_GENERATE_HEADER
# define RT_RUNTIME_LOADER_GENERATE_HEADER
# define RT_RUNTIME_LOADER_GENERATE_DECLS
# include <iprt/runtime-loader.h>
# undef RT_RUNTIME_LOADER_GENERATE_HEADER
# undef RT_RUNTIME_LOADER_GENERATE_DECLS

#elif defined(VBOX_FUSE_GENERATE_BODY)
# define RT_RUNTIME_LOADER_GENERATE_BODY_STUBS
# include <iprt/runtime-loader.h>
# undef RT_RUNTIME_LOADER_GENERATE_BODY_STUBS

#else
# error This file should only be included to generate stubs for loading the Fuse library at runtime
#endif

#undef RT_RUNTIME_LOADER_LIB_NAME
#undef RT_RUNTIME_LOADER_INSERT_SYMBOLS
