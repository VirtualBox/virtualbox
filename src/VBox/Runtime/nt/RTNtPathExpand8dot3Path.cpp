/* $Id: RTNtPathExpand8dot3Path.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Native NT, RTNtPathExpand8dot3Path.
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
#define LOG_GROUP RTLOGGROUP_FS
#if !defined(IPRT_NT_MAP_TO_ZW) && defined(IN_RING0)
# define IPRT_NT_MAP_TO_ZW
#endif
#ifdef IN_SUP_HARDENED_R3
# include <iprt/nt/nt-and-windows.h>
#else
# include <iprt/nt/nt.h>
#endif

#include <iprt/mem.h>
#include <iprt/errcore.h>
#include <iprt/string.h>



/**
 * Fixes up a path possibly containing one or more alternative 8-dot-3 style
 * components.
 *
 * The path is fixed up in place.  Errors are ignored.
 *
 * @returns VINF_SUCCESS if it all went smoothly, informational status codes
 *          indicating the nature of last problem we ran into.
 *
 * @param   pUniStr     The path to fix up. MaximumLength is the max buffer
 *                      length.
 * @param   fPathOnly   Whether to only process the path and leave the filename
 *                      as passed in.
 */
RTDECL(int) RTNtPathExpand8dot3Path(PUNICODE_STRING pUniStr, bool fPathOnly)
{
    int rc = VINF_SUCCESS;

    /*
     * We could use FileNormalizedNameInformation here and slap the volume device
     * path in front of the result, but it's only supported since windows 8.0
     * according to some docs... So we expand all supicious names.
     */
    union fix8dot3tmp
    {
        FILE_BOTH_DIR_INFORMATION Info;
        uint8_t abBuffer[sizeof(FILE_BOTH_DIR_INFORMATION) + 2048 * sizeof(WCHAR)];
    } *puBuf = NULL;


    PRTUTF16 pwszFix = pUniStr->Buffer;
    while (*pwszFix)
    {
        pwszFix = RTNtPathFindPossible8dot3Name(pwszFix);
        if (pwszFix == NULL)
            break;

        RTUTF16 wc;
        PRTUTF16 pwszFixEnd = pwszFix;
        while ((wc = *pwszFixEnd) != '\0' && wc != '\\' && wc != '/')
            pwszFixEnd++;
        if (wc == '\0' && fPathOnly)
            break;

        if (!puBuf)
        {
            puBuf = (union fix8dot3tmp *)RTMemAlloc(sizeof(*puBuf));
            if (!puBuf)
            {
                rc = -VERR_NO_MEMORY;
                break;
            }
        }

        RTUTF16 const wcSaved = *pwszFix;
        *pwszFix = '\0';                     /* paranoia. */

        UNICODE_STRING      NtDir;
        NtDir.Buffer = pUniStr->Buffer;
        NtDir.Length = NtDir.MaximumLength = (USHORT)((pwszFix - pUniStr->Buffer) * sizeof(WCHAR));

        HANDLE              hDir  = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;

        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtDir, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
#ifdef IN_RING0
        ObjAttr.Attributes |= OBJ_KERNEL_HANDLE;
#endif

        NTSTATUS rcNt = NtCreateFile(&hDir,
                                     FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                     &ObjAttr,
                                     &Ios,
                                     NULL /* Allocation Size*/,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     FILE_OPEN,
                                     FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);
        *pwszFix = wcSaved;
        if (NT_SUCCESS(rcNt))
        {
            RT_ZERO(*puBuf);

            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            UNICODE_STRING  NtFilterStr;
            NtFilterStr.Buffer = pwszFix;
            NtFilterStr.Length = (USHORT)((uintptr_t)pwszFixEnd - (uintptr_t)pwszFix);
            NtFilterStr.MaximumLength = NtFilterStr.Length;
            rcNt = NtQueryDirectoryFile(hDir,
                                        NULL /* Event */,
                                        NULL /* ApcRoutine */,
                                        NULL /* ApcContext */,
                                        &Ios,
                                        puBuf,
                                        sizeof(*puBuf) - sizeof(WCHAR),
                                        FileBothDirectoryInformation,
                                        FALSE /*ReturnSingleEntry*/,
                                        &NtFilterStr,
                                        FALSE /*RestartScan */);
            if (NT_SUCCESS(rcNt) && puBuf->Info.NextEntryOffset == 0) /* There shall only be one entry matching... */
            {
                uint32_t offName = puBuf->Info.FileNameLength / sizeof(WCHAR);
                while (offName > 0  && puBuf->Info.FileName[offName - 1] != '\\' && puBuf->Info.FileName[offName - 1] != '/')
                    offName--;
                uint32_t cwcNameNew = (puBuf->Info.FileNameLength / sizeof(WCHAR)) - offName;
                uint32_t cwcNameOld = (uint32_t)(pwszFixEnd - pwszFix);

                if (cwcNameOld == cwcNameNew)
                    memcpy(pwszFix, &puBuf->Info.FileName[offName], cwcNameNew * sizeof(WCHAR));
                else if (   pUniStr->Length + cwcNameNew * sizeof(WCHAR) - cwcNameOld * sizeof(WCHAR) + sizeof(WCHAR)
                         <= pUniStr->MaximumLength)
                {
                    size_t cwcLeft = pUniStr->Length - (pwszFixEnd - pUniStr->Buffer) * sizeof(WCHAR) + sizeof(WCHAR);
                    memmove(&pwszFix[cwcNameNew], pwszFixEnd, cwcLeft * sizeof(WCHAR));
                    pUniStr->Length -= (USHORT)(cwcNameOld * sizeof(WCHAR));
                    pUniStr->Length += (USHORT)(cwcNameNew * sizeof(WCHAR));
                    pwszFixEnd      -= cwcNameOld;
                    pwszFixEnd      += cwcNameNew;
                    memcpy(pwszFix, &puBuf->Info.FileName[offName], cwcNameNew * sizeof(WCHAR));
                }
                else
                    rc = VINF_BUFFER_OVERFLOW;
            }
            else if (NT_SUCCESS(rcNt))
                rc = -VERR_DUPLICATE;
            else
            {
                rc = -RTErrConvertFromNtStatus(rcNt);
                if (rc < 0)
                    rc = -rc;
            }

            NtClose(hDir);
        }
        else
            rc = -RTErrConvertFromNtStatus(rcNt);

        /* Advance */
        pwszFix = pwszFixEnd;
    }

    if (puBuf)
        RTMemFree(puBuf);

    if (pUniStr->Length < pUniStr->MaximumLength)
        pUniStr->Buffer[pUniStr->Length / sizeof(WCHAR)] = '\0';

    return rc;
}

