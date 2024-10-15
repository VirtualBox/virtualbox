/* $Id: manifest-file.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Manifest, the bits with file dependencies
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/manifest.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/vfs.h>

#include "internal/magics.h"



RTDECL(int) RTManifestReadStandardFromFile(RTMANIFEST hManifest, const char *pszFilename)
{
    RTFILE      hFile;
    uint32_t    fFlags = RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_OPEN;
    int rc = RTFileOpen(&hFile, pszFilename, fFlags);
    if (RT_SUCCESS(rc))
    {
        RTVFSIOSTREAM hVfsIos;
        rc = RTVfsIoStrmFromRTFile(hFile, fFlags, true /*fLeaveOpen*/, &hVfsIos);
        if (RT_SUCCESS(rc))
        {
            rc = RTManifestReadStandard(hManifest, hVfsIos);
            RTVfsIoStrmRelease(hVfsIos);
        }
        RTFileClose(hFile);
    }
    return rc;
}


RTDECL(int) RTManifestWriteStandardToFile(RTMANIFEST hManifest, const char *pszFilename)
{
    RTFILE      hFile;
    uint32_t    fFlags = RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE;
    int rc = RTFileOpen(&hFile, pszFilename, fFlags);
    if (RT_SUCCESS(rc))
    {
        RTVFSIOSTREAM hVfsIos;
        rc = RTVfsIoStrmFromRTFile(hFile, fFlags, true /*fLeaveOpen*/, &hVfsIos);
        if (RT_SUCCESS(rc))
        {
            rc = RTManifestWriteStandard(hManifest, hVfsIos);
            RTVfsIoStrmRelease(hVfsIos);
        }
        RTFileClose(hFile);
    }
    return rc;
}

