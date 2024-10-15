/* $Id: assert.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Replaces C runtime assert with a simplified version which just hits breakpoint.
 *
 * Mesa code uses assert.h a lot, which is inconvenient because the C runtime
 * implementation wants to open a message box and it does not work in the
 * graphics driver.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_3D_MESA_assert_h
#define GA_INCLUDED_3D_MESA_assert_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/asm.h>

#undef assert
#ifdef DEBUG
#define assert(_e) (void)( (!!(_e)) || (ASMBreakpoint(), 0) )
#else
#define assert(_e) (void)(0)
#endif

#endif /* !GA_INCLUDED_3D_MESA_assert_h */
