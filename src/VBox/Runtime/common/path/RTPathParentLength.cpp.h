/* $Id: RTPathParentLength.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathParentLength - Code Template.
 *
 * This file included multiple times with different path style macros.
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

#include "rtpath-root-length-template.cpp.h"

/**
 * @copydoc RTPathParentLengthEx
 */
static size_t RTPATH_STYLE_FN(rtPathParentLength)(const char *pszPath, uint32_t fFlags)
{
    /*
     * Determin the length of the root component so we can make sure
     * we don't try ascend higher than it.
     */
    size_t const cchRoot = RTPATH_STYLE_FN(rtPathRootLengthEx)(pszPath, fFlags);

    /*
     * Rewind to the start of the final component.
     */
    size_t cch = strlen(pszPath);

    /* Trailing slashes: */
    while (cch > cchRoot && RTPATH_IS_SLASH(pszPath[cch - 1]))
        cch--;

    /* The component: */
    while (cch > cchRoot && !RTPATH_IS_SEP(pszPath[cch - 1]))
        cch--;

    /* Done! */
    return cch;
}

