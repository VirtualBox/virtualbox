/* $Id: bs3kit-mangling-code.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Symbol mangling, code.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * Do function mangling.  This can be redone at compile time (templates).
 */
#undef BS3_CMN_MANGLER
#undef BS3_MODE_MANGLER
#if ARCH_BITS != 16 || !defined(BS3_USE_ALT_16BIT_TEXT_SEG)
# define BS3_CMN_MANGLER(a_Function)            BS3_CMN_NM(a_Function)
# define BS3_MODE_MANGLER(a_Function)           TMPL_NM(a_Function)
#else
# define BS3_CMN_MANGLER(a_Function)            BS3_CMN_FAR_NM(a_Function)
# define BS3_MODE_MANGLER(a_Function)           TMPL_FAR_NM(a_Function)
#endif
#include "bs3kit-mangling-code-undef.h"
#include "bs3kit-mangling-code-define.h"

