/* $Id: symdb.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal Header for the NT Ring-0 Driver Symbol DB.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_nt_symdb_h
#define IPRT_INCLUDED_SRC_r0drv_nt_symdb_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>


/**
 * NT Version info.
 */
typedef struct RTNTSDBOSVER
{
    /** The major version number. */
    uint8_t     uMajorVer;
    /** The minor version number. */
    uint8_t     uMinorVer;
    /** Set if checked build, clear if free (retail) build. */
    uint8_t     fChecked : 1;
    /** Set if multi processor kernel. */
    uint8_t     fSmp : 1;
    /** The service pack number. */
    uint8_t     uCsdNo : 6;
    /** The build number. */
    uint32_t    uBuildNo;
} RTNTSDBOSVER;
/** Pointer to NT version info. */
typedef RTNTSDBOSVER *PRTNTSDBOSVER;
/** Pointer to const NT version info. */
typedef RTNTSDBOSVER const *PCRTNTSDBOSVER;


/**
 * Compare NT OS version structures.
 *
 * @retval  0 if equal
 * @retval  1 if @a pInfo1 is newer/greater than @a pInfo2
 * @retval  -1 if @a pInfo1 is older/less than @a pInfo2
 *
 * @param   pInfo1              The first version info structure.
 * @param   pInfo2              The second version info structure.
 */
DECLINLINE(int) rtNtOsVerInfoCompare(PCRTNTSDBOSVER pInfo1, PCRTNTSDBOSVER pInfo2)
{
    if (pInfo1->uMajorVer != pInfo2->uMajorVer)
        return pInfo1->uMajorVer > pInfo2->uMajorVer ? 1 : -1;
    if (pInfo1->uMinorVer != pInfo2->uMinorVer)
        return pInfo1->uMinorVer > pInfo2->uMinorVer ? 1 : -1;
    if (pInfo1->uBuildNo  != pInfo2->uBuildNo)
        return pInfo1->uBuildNo  > pInfo2->uBuildNo  ? 1 : -1;
    if (pInfo1->uCsdNo    != pInfo2->uCsdNo)
        return pInfo1->uCsdNo    > pInfo2->uCsdNo    ? 1 : -1;
    if (pInfo1->fSmp      != pInfo2->fSmp)
        return pInfo1->fSmp      > pInfo2->fSmp      ? 1 : -1;
    if (pInfo1->fChecked  != pInfo2->fChecked)
        return pInfo1->fChecked  > pInfo2->fChecked  ? 1 : -1;
    return 0;
}

#endif /* !IPRT_INCLUDED_SRC_r0drv_nt_symdb_h */

