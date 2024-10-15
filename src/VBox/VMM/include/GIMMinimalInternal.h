/* $Id: GIMMinimalInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Minimal, Internal header file.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_GIMMinimalInternal_h
#define VMM_INCLUDED_SRC_include_GIMMinimalInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <VBox/types.h>

RT_C_DECLS_BEGIN

#ifdef IN_RING3
VMMR3_INT_DECL(int)         gimR3MinimalInit(PVM pVM);
VMMR3_INT_DECL(int)         gimR3MinimalInitCompleted(PVM pVM);
VMMR3_INT_DECL(void)        gimR3MinimalRelocate(PVM pVM, RTGCINTPTR offDelta);
#endif /* IN_RING3 */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_GIMMinimalInternal_h */

