/* $Id: bs3-cmn-instantiate-common.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Common template instantiator body.
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
 * Instantiating common code (c16, c32, c64).
 * This must be done first.
 */

/** @def BS3_INSTANTIATING_CMN
 * @ingroup grp_bs3kit_tmpl
 * Indicates that we're instantiating common code (c16, c32, c64).
 */
#define BS3_INSTANTIATING_CMN

#ifdef BS3_CMN_INSTANTIATE_FILE1

# define BS3_CMN_INSTANTIATE_FILE1_B <BS3_CMN_INSTANTIATE_FILE1>

# if ARCH_BITS == 16 /* 16-bit - real mode. */
#  define TMPL_MODE BS3_MODE_RM
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_CMN_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>
# endif

# if ARCH_BITS == 32 /* 32-bit - paged protected mode. */
#  define TMPL_MODE BS3_MODE_PP32
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_CMN_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>
# endif

# if ARCH_BITS == 64 /* 64-bit. */
#  define TMPL_MODE BS3_MODE_LM64
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_CMN_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>
# endif

#endif

#undef BS3_INSTANTIATING_CMN


/*
 * Instantiating code for each individual mode (rm, pe16, pe16_32, ...).
 */

/** @def BS3_INSTANTIATING_MODE
 * @ingroup grp_bs3kit_tmpl
 * Indicates that we're instantiating mode specific code (rm, pe16, ...).
 */
#define BS3_INSTANTIATING_MODE

#ifdef BS3_MODE_INSTANTIATE_FILE1

# define BS3_MODE_INSTANTIATE_FILE1_B <BS3_MODE_INSTANTIATE_FILE1>

# if ARCH_BITS == 16 /* 16-bit */

#  define TMPL_MODE BS3_MODE_RM
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PE16
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PE16_V86
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PE32_16
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PEV86
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PP16
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PP16_V86
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PP32_16
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PPV86
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PAE16
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PAE16_V86
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PAE32_16
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PAEV86
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_LM16
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

# endif

# if ARCH_BITS == 32 /* 32-bit  */

#  define TMPL_MODE BS3_MODE_PE16_32
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PE32
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PP16_32
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PP32
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PAE16_32
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_PAE32
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

#  define TMPL_MODE BS3_MODE_LM32
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>

# endif

# if ARCH_BITS == 64 /* 64-bit. */
#  define TMPL_MODE BS3_MODE_LM64
#  include <bs3kit/bs3kit-template-header.h>
#  include BS3_MODE_INSTANTIATE_FILE1_B
#  include <bs3kit/bs3kit-template-footer.h>
# endif

#endif

