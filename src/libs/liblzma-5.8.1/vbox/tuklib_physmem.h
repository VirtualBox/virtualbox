/* $Id: tuklib_physmem.h 109441 2025-05-06 17:40:59Z samantha.scholz@oracle.com $ */
/** @file
 * tuklib_physmem.h - Memory information.
 */

/*
 * Copyright (C) 2023 Oracle and/or its affiliates.
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

#ifndef TUKLIB_PHYSMEM_H
#define TUKLIB_PHYSMEM_H

#include "sysdefs.h"
RT_C_DECLS_BEGIN

DECL_FORCE_INLINE(uint64_t) tuklib_physmem(void)
{
	uint64_t cbMemAvail = 0;
	int rc = RTSystemQueryAvailableRam(&cbMemAvail);
	if (RT_FAILURE(rc))
		cbMemAvail = 0;
	return cbMemAvail;
}

RT_C_DECLS_END
#endif
