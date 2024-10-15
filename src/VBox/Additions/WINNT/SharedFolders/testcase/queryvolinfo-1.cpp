/* $Id: queryvolinfo-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders FSD - Simple Testcase.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <iprt/nt/nt-and-windows.h>
#include <stdio.h>
#include <string.h>

static const char * const g_apszVolInfoNames[] =
{
    "0",
    "FileFsVolumeInformation",
    "FileFsLabelInformation",
    "FileFsSizeInformation",          /**< FILE_FS_SIZE_INFORMATION */
    "FileFsDeviceInformation",
    "FileFsAttributeInformation",
    "FileFsControlInformation",
    "FileFsFullSizeInformation",
    "FileFsObjectIdInformation",
    "FileFsDriverPathInformation",
    "FileFsVolumeFlagsInformation",
    "FileFsSectorSizeInformation",
    "FileFsDataCopyInformation",
    "FileFsMaximumInformation",
    "FileFsMaximumInformation+1",
    "FileFsMaximumInformation+2",
    "FileFsMaximumInformation+3",
    "FileFsMaximumInformation+4",
    "FileFsMaximumInformation+5",
    "FileFsMaximumInformation+6",
    "FileFsMaximumInformation+7",
    "FileFsMaximumInformation+8",
    "FileFsMaximumInformation+9",
};

static const char *GetStatusName(NTSTATUS rcNt)
{
    switch (rcNt)
    {
        case STATUS_SUCCESS:                    return " (STATUS_SUCCESS)";
        case STATUS_INVALID_INFO_CLASS:         return " (STATUS_INVALID_INFO_CLASS)";
        case STATUS_INVALID_PARAMETER:          return " (STATUS_INVALID_PARAMETER)";
        case STATUS_INVALID_DEVICE_REQUEST:     return " (STATUS_INVALID_DEVICE_REQUEST)";
        case STATUS_NO_SUCH_DEVICE:             return " (STATUS_NO_SUCH_DEVICE)";
        case STATUS_NOT_SUPPORTED:              return " (STATUS_NOT_SUPPORTED)";
    }
    return "";
}

static void DoQueries(HANDLE hFile)
{
    union
    {
        uint8_t abBuf[4096];
        struct
        {
            LARGE_INTEGER VolumeCreationTime;
            ULONG   VolumeSerialNumber;
            ULONG   VolumeLabelLength;
            BOOLEAN SupportsObjects;
            BOOLEAN Padding;
            WCHAR   VolumeLabel[63];
        } VolInfo;
    } uBuf;

    IO_STATUS_BLOCK const VirginIos = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    for (unsigned iClass = 0; iClass < RT_ELEMENTS(g_apszVolInfoNames); iClass++)
    {
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NTSTATUS rcNt = NtQueryVolumeInformationFile(hFile, &Ios, &uBuf, sizeof(uBuf), (FS_INFORMATION_CLASS)iClass);
        printf("  %45s: rcNt=%#x%s", g_apszVolInfoNames[iClass], rcNt, GetStatusName(rcNt));
        if (   Ios.Information == VirginIos.Information
            && Ios.Status == VirginIos.Status)
            printf(" Ios=<not modified>\n", Ios.Status, Ios.Information);
        else
            printf(" Ios.Status=%#x%s Ios.Information=%p\n", Ios.Status, GetStatusName(Ios.Status), Ios.Information);
        if (NT_SUCCESS(rcNt))
        {
            ULONG const cbNominal = Ios.Information;
            for (ULONG cbLess = 0; cbLess < 8; cbLess++)
            {
                memset(&uBuf, 0xff, sizeof(uBuf));
                rcNt = NtQueryVolumeInformationFile(hFile, &Ios, &uBuf, cbNominal - cbLess, (FS_INFORMATION_CLASS)iClass);
                printf("  %45s    cbBuf=%u -> rcNt=%#x%s", "", cbNominal - cbLess, rcNt, GetStatusName(rcNt));
                switch (iClass)
                {
                    case FileFsVolumeInformation:
                    {
                        printf(" VolNmLen=%#x:", uBuf.VolInfo.VolumeLabelLength);
                        size_t const cwcMax = (  cbNominal - cbLess - 18 < uBuf.VolInfo.VolumeLabelLength
                                               ? cbNominal - cbLess - 18 : uBuf.VolInfo.VolumeLabelLength) / sizeof(WCHAR);
                        for (unsigned off = 0; off < cwcMax; off++)
                            printf(" %02x", uBuf.VolInfo.VolumeLabel[off]);
                        break;
                    }
                }
                if (   Ios.Information == VirginIos.Information
                    && Ios.Status == VirginIos.Status)
                    printf(" Ios=<not modified>\n", Ios.Status, Ios.Information);
                else
                    printf(" Ios.Status=%#x%s Ios.Information=%p\n", Ios.Status, GetStatusName(Ios.Status), Ios.Information);
            }
        }
    }
}


int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        printf("Querying info for: %s\n", argv[i]);
        HANDLE hFile = CreateFileA(argv[i], GENERIC_READ, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL /*pSecAttr*/, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                   NULL /*hTemplate*/);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            DoQueries(hFile);
            CloseHandle(hFile);
        }
        else
            fprintf(stderr, "error opening '%s': %u\n", GetLastError());
    }
    return 0;
}

