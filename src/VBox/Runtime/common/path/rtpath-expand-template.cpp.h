/* $Id: rtpath-expand-template.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPath - Internal header that includes RTPATH_TEMPLATE_CPP_H multiple
 *                 times to expand the code for different path styles.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#undef  RTPATH_DELIMITER

/*
 * DOS style
 */
#undef  RTPATH_STYLE
#undef  RTPATH_SLASH
#undef  RTPATH_SLASH_STR
#undef  RTPATH_IS_SLASH
#undef  RTPATH_IS_VOLSEP
#undef  RTPATH_STYLE_FN

#define RTPATH_STYLE            RTPATH_STR_F_STYLE_DOS
#define RTPATH_SLASH            '\\'
#define RTPATH_SLASH_STR        "\\"
#define RTPATH_IS_SLASH(a_ch)   ( (a_ch) == '\\' || (a_ch) == '/' )
#define RTPATH_IS_VOLSEP(a_ch)  ( (a_ch) == ':' )
#define RTPATH_STYLE_FN(a_Name) a_Name ## StyleDos
#include RTPATH_TEMPLATE_CPP_H

/*
 * Unix style.
 */
#undef  RTPATH_STYLE
#undef  RTPATH_SLASH
#undef  RTPATH_SLASH_STR
#undef  RTPATH_IS_SLASH
#undef  RTPATH_IS_VOLSEP
#undef  RTPATH_STYLE_FN

#define RTPATH_STYLE            RTPATH_STR_F_STYLE_UNIX
#define RTPATH_SLASH            '/'
#define RTPATH_SLASH_STR        "/"
#define RTPATH_IS_SLASH(a_ch)   ( (a_ch) == '/' )
#define RTPATH_IS_VOLSEP(a_ch)  ( false )
#define RTPATH_STYLE_FN(a_Name) a_Name ## StyleUnix
#include RTPATH_TEMPLATE_CPP_H

/*
 * Clean up and restore the host style.
 */
#undef RTPATH_STYLE_FN
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# undef  RTPATH_STYLE
# undef  RTPATH_SLASH
# undef  RTPATH_SLASH_STR
# undef  RTPATH_IS_SLASH
# undef  RTPATH_IS_VOLSEP
# define RTPATH_STYLE           RTPATH_STR_F_STYLE_DOS
# define RTPATH_SLASH           '\\'
# define RTPATH_SLASH_STR       "\\"
# define RTPATH_IS_SLASH(a_ch)  ( (a_ch) == '\\' || (a_ch) == '/' )
# define RTPATH_IS_VOLSEP(a_ch) ( (a_ch) == ':' )
#endif

