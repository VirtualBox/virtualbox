/* $Id: RTFileQueryFsSizes-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileQueryFsSizes, Native NT.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_FILE
#include "internal-r3-nt.h"

#include <iprt/file.h>
#include <iprt/errcore.h>



RTR3DECL(int) RTFileQueryFsSizes(RTFILE hFile, PRTFOFF pcbTotal, RTFOFF *pcbFree,
                                 uint32_t *pcbBlock, uint32_t *pcbSector)
{
    int rc;

    /*
     * Get the volume information.
     */
    FILE_FS_SIZE_INFORMATION FsSizeInfo;
    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    NTSTATUS rcNt = NtQueryVolumeInformationFile((HANDLE)RTFileToNative(hFile), &Ios,
                                                 &FsSizeInfo, sizeof(FsSizeInfo), FileFsSizeInformation);
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Calculate the return values.
         */
        if (pcbTotal)
        {
            *pcbTotal = FsSizeInfo.TotalAllocationUnits.QuadPart
                      * FsSizeInfo.SectorsPerAllocationUnit
                      * FsSizeInfo.BytesPerSector;
            if (   *pcbTotal / FsSizeInfo.SectorsPerAllocationUnit / FsSizeInfo.BytesPerSector
                != FsSizeInfo.TotalAllocationUnits.QuadPart)
                *pcbTotal = UINT64_MAX;
        }

        if (pcbFree)
        {
            *pcbFree = FsSizeInfo.AvailableAllocationUnits.QuadPart
                     * FsSizeInfo.SectorsPerAllocationUnit
                     * FsSizeInfo.BytesPerSector;
            if (   *pcbFree / FsSizeInfo.SectorsPerAllocationUnit / FsSizeInfo.BytesPerSector
                != FsSizeInfo.AvailableAllocationUnits.QuadPart)
                *pcbFree = UINT64_MAX;
        }

        rc = VINF_SUCCESS;
        if (pcbBlock)
        {
            *pcbBlock  = FsSizeInfo.SectorsPerAllocationUnit * FsSizeInfo.BytesPerSector;
            if (*pcbBlock / FsSizeInfo.BytesPerSector != FsSizeInfo.SectorsPerAllocationUnit)
                rc = VERR_OUT_OF_RANGE;
        }

        if (pcbSector)
            *pcbSector = FsSizeInfo.BytesPerSector;
    }
    else
        rc = RTErrConvertFromNtStatus(rcNt);

    return rc;
}

