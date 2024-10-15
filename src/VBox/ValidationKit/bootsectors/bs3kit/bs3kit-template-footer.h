/* $Id: bs3kit-template-footer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit footer for multi-mode code templates.
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
 * Undefine macros defined by the header.
 * This is a subset of what bs3kit-template-footer.mac does.
 */
#undef TMPL_RM
#undef TMPL_PE16
#undef TMPL_PE16_32
#undef TMPL_PE16_V86
#undef TMPL_PE32
#undef TMPL_PE32_16
#undef TMPL_PEV86
#undef TMPL_PP16
#undef TMPL_PP16_32
#undef TMPL_PP16_V86
#undef TMPL_PP32
#undef TMPL_PP32_16
#undef TMPL_PPV86
#undef TMPL_PAE16
#undef TMPL_PAE16_32
#undef TMPL_PAE16_V86
#undef TMPL_PAE32
#undef TMPL_PAE32_16
#undef TMPL_PAEV86
#undef TMPL_LM16
#undef TMPL_LM32
#undef TMPL_LM64

#undef TMPL_CMN_PE
#undef TMPL_SYS_PE16
#undef TMPL_SYS_PE32
#undef TMPL_CMN_PP
#undef TMPL_SYS_PP16
#undef TMPL_SYS_PP32
#undef TMPL_CMN_PAE
#undef TMPL_SYS_PAE16
#undef TMPL_SYS_PAE32
#undef TMPL_CMN_LM
#undef TMPL_CMN_V86
#undef TMPL_CMN_R86
#undef TMPL_CMN_PAGING
#undef TMPL_CMN_WEIRD
#undef TMPL_CMN_WEIRD_V86

#undef TMPL_CMN_R86

#undef TMPL_NM
#undef TMPL_FAR_NM
#undef TMPL_MODE
#undef TMPL_MODE_STR
#undef TMPL_MODE_LNAME
#undef TMPL_MODE_UNAME
#undef TMPL_16BIT
#undef TMPL_32BIT
#undef TMPL_64BIT
#undef TMPL_BITS

