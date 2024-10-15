/* $Id: internal-r3-nt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal Header for the Native NT code.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r3_nt_internal_r3_nt_h
#define IPRT_INCLUDED_SRC_r3_nt_internal_r3_nt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef IN_SUP_HARDENED_R3
# include <iprt/nt/nt-and-windows.h>
#else
# include <iprt/nt/nt.h>
#endif
#include "internal/iprt.h"


#if 1
/** Enables the "\\!\" NT path pass thru as well as hacks for listing NT object
 * directories. */
# define IPRT_WITH_NT_PATH_PASSTHRU 1
#endif



/**
 * Internal helper for comparing a WCHAR string with a char string.
 *
 * @returns @c true if equal, @c false if not.
 * @param   pwsz1               The first string.
 * @param   cch1                The length of the first string, in bytes.
 * @param   psz2                The second string.
 * @param   cch2                The length of the second string.
 */
DECLINLINE(bool) rtNtCompWideStrAndAscii(WCHAR const *pwsz1, size_t cch1, const char *psz2, size_t cch2)
{
    if (cch1 != cch2 * 2)
        return false;
    while (cch2-- > 0)
    {
        unsigned ch1 = *pwsz1++;
        unsigned ch2 = (unsigned char)*psz2++;
        if (ch1 != ch2)
            return false;
    }
    return true;
}

#endif /* !IPRT_INCLUDED_SRC_r3_nt_internal_r3_nt_h */

/**
 * Common worker for RTFileSetMode, RTPathSetMode and RTDirRelPathSetMode.
 *
 * @returns IPRT status code.
 * @param   hNativeFile The NT handle to the file system object.
 * @param   fMode       Valid and normalized file mode mask to set.
 */
DECLHIDDEN(int) rtNtFileSetModeWorker(HANDLE hNativeFile, RTFMODE fMode);

