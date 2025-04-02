/* $Id: nathandletable.h 109046 2025-04-02 18:40:55Z jack.doherty@oracle.com $ */
/** @file
 * libslirp: NAT Handle Table Singleton Wrapper
 */

/*
 * Copyright (C) 2006-2023 Oracle and/or its affiliates.
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

#ifndef INCLUDED_nathandletable_h
#define INCLUDED_nathandletable_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef _WIN32
# include <iprt/assert.h>
# include <iprt/err.h>
# include <iprt/handletable.h>
# include <iprt/mem.h>

# ifndef LOG_GROUP
#  define LOG_GROUP LOG_GROUP_DRV_NAT
#  include <VBox/log.h>
# endif

# include <winsock2.h>

RT_C_DECLS_BEGIN

extern RTHANDLETABLE g_hNATHandleTable;

/**
 * Looks up a SOCKET handle by the integer handle used by libslirp.
 *
 * @returns actual SOCKET handle used by Windows
 * @param   fd            Integer handle used internally by libslirp.
 */
SOCKET libslirp_wrap_RTHandleTableLookup(int fd);

/**
 * Allocates an integer handle from a SOCKET handle for use libslirp.
 *
 * @returns VBox status code
 * @param   s            SOCKET handle from Windows.
 *                       Typically from a socket() call.
 * @param   h            Return param. Integer handle from table.
 */
int libslirp_wrap_RTHandleTableAlloc(SOCKET s, uint32_t *h);

/**
 * Frees entry from lookup table.
 *
 * @returns VBox status code
 * @param   fd            Integer handle used internally by libslirp.
 */
int libslirp_wrap_RTHandleTableFree(int fd);

RT_C_DECLS_END

# endif /* _WIN32*/

#endif