/* $Id: DynLoadLibSolaris.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Dynamically loaded libraries for Solaris hosts, Internal header.
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

#ifndef MAIN_INCLUDED_SRC_src_server_solaris_DynLoadLibSolaris_h
#define MAIN_INCLUDED_SRC_src_server_solaris_DynLoadLibSolaris_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define LIB_DLPI "libdlpi.so.1"
#ifdef RT_OS_SOLARIS_10
#include <sys/dlpi.h>
#else
#include <libdlpi.h>
#endif

typedef boolean_t dlpi_walkfunc_t(const char*, void *);

extern int  (*g_pfnLibDlpiWalk)(dlpi_walkfunc_t *, void *, uint_t);
extern int  (*g_pfnLibDlpiOpen)(const char *, dlpi_handle_t *, uint_t);
extern void (*g_pfnLibDlpiClose)(dlpi_handle_t);

extern bool VBoxSolarisLibDlpiFound(void);

#endif /* !MAIN_INCLUDED_SRC_src_server_solaris_DynLoadLibSolaris_h */

