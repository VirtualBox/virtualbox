/* $Id: HBDMgmt-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: Host block device management API.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_VD
#include <VBox/cdefs.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/list.h>

#include <iprt/nt/nt-and-windows.h>
#include <iprt/win/windows.h>

#include "HBDMgmt.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Claimed block device state.
 */
typedef struct HBDMGRDEV
{
    /** List node. */
    RTLISTNODE         ListNode;
    /** The block device name. */
    char              *pszDevice;
    /** Number of volumes for this block device. */
    unsigned           cVolumes;
    /** Array of handle to the volumes for unmounting and taking it offline. */
    HANDLE             ahVolumes[1];
} HBDMGRDEV;
/** Pointer to a claimed block device. */
typedef HBDMGRDEV *PHBDMGRDEV;

/**
 * Internal Host block device manager state.
 */
typedef struct HBDMGRINT
{
    /** List of claimed block devices. */
    RTLISTANCHOR       ListClaimed;
    /** Fast mutex protecting the list. */
    RTSEMFASTMUTEX     hMtxList;
} HBDMGRINT;
/** Pointer to an interal block device manager state. */
typedef HBDMGRINT *PHBDMGRINT;

#define HBDMGR_NT_HARDDISK_START "\\Device\\Harddisk"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Unclaims the given block device and frees its state removing it from the list.
 *
 * @param   pDev           The block device to unclaim.
 */
static void hbdMgrDevUnclaim(PHBDMGRDEV pDev)
{
    LogFlowFunc(("pDev=%p{%s} cVolumes=%u\n", pDev, pDev->pszDevice, pDev->cVolumes));

    for (unsigned i = 0; i < pDev->cVolumes; i++)
    {
        DWORD dwReturned = 0;

        LogFlowFunc(("Taking volume %u online\n", i));
        BOOL bRet = DeviceIoControl(pDev->ahVolumes[i], IOCTL_VOLUME_ONLINE, NULL, 0, NULL, 0, &dwReturned, NULL);
        if (!bRet)
            LogRel(("HBDMgmt: Failed to take claimed volume online during cleanup: %s{%Rrc}\n",
                    pDev->pszDevice, RTErrConvertFromWin32(GetLastError())));

        CloseHandle(pDev->ahVolumes[i]);
    }

    RTListNodeRemove(&pDev->ListNode);
    RTStrFree(pDev->pszDevice);
    RTMemFree(pDev);
}

/**
 * Returns the block device given by the filename if claimed or NULL.
 *
 * @returns Pointer to the claimed block device or NULL if not claimed.
 * @param   pThis          The block device manager.
 * @param   pszFilename    The name to look for.
 */
static PHBDMGRDEV hbdMgrDevFindByName(PHBDMGRINT pThis, const char *pszFilename)
{
    bool fFound = false;

    PHBDMGRDEV pIt;
    RTListForEach(&pThis->ListClaimed, pIt, HBDMGRDEV, ListNode)
    {
        if (!RTStrCmp(pszFilename, pIt->pszDevice))
        {
            fFound = true;
            break;
        }
    }

    return fFound ? pIt : NULL;
}

/**
 * Queries the target in the NT namespace of the given symbolic link.
 *
 * @returns VBox status code.
 * @param   pwszLinkNt      The symbolic link to query the target for.
 * @param   ppwszLinkTarget Where to store the link target in the NT namespace on success.
 *                          Must be freed with RTUtf16Free().
 */
static int hbdMgrQueryNtLinkTarget(PRTUTF16 pwszLinkNt, PRTUTF16 *ppwszLinkTarget)
{
    int                 rc    = VINF_SUCCESS;
    HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    UNICODE_STRING      NtName;

    NtName.Buffer        = (PWSTR)pwszLinkNt;
    NtName.Length        = (USHORT)(RTUtf16Len(pwszLinkNt) * sizeof(RTUTF16));
    NtName.MaximumLength = NtName.Length + sizeof(RTUTF16);

    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

    NTSTATUS rcNt = NtOpenSymbolicLinkObject(&hFile, SYMBOLIC_LINK_QUERY, &ObjAttr);
    if (NT_SUCCESS(rcNt))
    {
        UNICODE_STRING UniStr;
        RTUTF16 awszBuf[1024];
        RT_ZERO(awszBuf);
        UniStr.Buffer = awszBuf;
        UniStr.MaximumLength = sizeof(awszBuf);
        rcNt = NtQuerySymbolicLinkObject(hFile, &UniStr, NULL);
        if (NT_SUCCESS(rcNt))
        {
            *ppwszLinkTarget = RTUtf16Dup((PRTUTF16)UniStr.Buffer);
            if (!*ppwszLinkTarget)
                rc = VERR_NO_STR_MEMORY;
        }
        else
            rc = RTErrConvertFromNtStatus(rcNt);

        CloseHandle(hFile);
    }
    else
        rc = RTErrConvertFromNtStatus(rcNt);

    return rc;
}

/**
 * Queries the harddisk volume device in the NT namespace for the given Win32
 * block device path.
 *
 * @returns VBox status code.
 * @param   pwszDriveWin32  The Win32 path to the block device (e.g. "\\.\PhysicalDrive0" for example)
 * @param   ppwszDriveNt    Where to store the NT path to the volume on success.
 *                          Must be freed with RTUtf16Free().
 */
static int hbdMgrQueryNtName(PRTUTF16 pwszDriveWin32, PRTUTF16 *ppwszDriveNt)
{
    int rc = VINF_SUCCESS;
    RTUTF16 awszFileNt[1024];

    /*
     * Make sure the path is at least 5 characters long so we can safely access
     * the part following \\.\ below.
     */
    AssertReturn(RTUtf16Len(pwszDriveWin32) >= 5, VERR_INVALID_STATE);

    RT_ZERO(awszFileNt);
    RTUtf16CatAscii(awszFileNt, RT_ELEMENTS(awszFileNt), "\\??\\");
    RTUtf16Cat(awszFileNt, RT_ELEMENTS(awszFileNt), &pwszDriveWin32[4]);

    /* Make sure there is no trailing \ at the end or we will fail. */
    size_t cwcPath = RTUtf16Len(awszFileNt);
    if (awszFileNt[cwcPath - 1] == L'\\')
        awszFileNt[cwcPath - 1] = L'\0';

    return hbdMgrQueryNtLinkTarget(awszFileNt, ppwszDriveNt);
}

static int hbdMgrQueryAllMountpointsForDisk(PRTUTF16 pwszDiskNt, PRTUTF16 **ppapwszVolumes,
                                            unsigned *pcVolumes)
{
    /*
     * Try to get the symlink target for every partition, we will take the easy approach
     * and just try to open every partition starting with \Device\Harddisk<N>\Partition1
     * (0 is a special reserved partition linking to the complete disk).
     *
     * For every partition we get the target \Device\HarddiskVolume<N> and query all mountpoints
     * with that.
     */
    int rc = VINF_SUCCESS;
    char *pszDiskNt = NULL;
    unsigned cVolumes = 0;
    unsigned cVolumesMax = 10;
    PRTUTF16 *papwszVolumes = (PRTUTF16 *)RTMemAllocZ(cVolumesMax * sizeof(PRTUTF16));

    if (!papwszVolumes)
        return VERR_NO_MEMORY;

    rc = RTUtf16ToUtf8(pwszDiskNt, &pszDiskNt);
    if (RT_SUCCESS(rc))
    {
        /* Check that the path matches our expectation \Device\Harddisk<N>\DR<N>. */
        if (!RTStrNCmp(pszDiskNt, HBDMGR_NT_HARDDISK_START, sizeof(HBDMGR_NT_HARDDISK_START) - 1))
        {
            uint32_t iDisk = 0;

            rc = RTStrToUInt32Ex(pszDiskNt + sizeof(HBDMGR_NT_HARDDISK_START) - 1, NULL, 10, &iDisk);
            if (RT_SUCCESS(rc) || rc == VWRN_TRAILING_CHARS)
            {
                uint32_t iPart = 1;

                /* Try to query all mount points for all partitions, the simple way. */
                do
                {
                    char aszDisk[1024];
                    RT_ZERO(aszDisk);

                    size_t cchWritten = RTStrPrintf(&aszDisk[0], sizeof(aszDisk), "\\Device\\Harddisk%u\\Partition%u", iDisk, iPart);
                    if (cchWritten < sizeof(aszDisk))
                    {
                        PRTUTF16 pwszDisk = NULL;
                        rc = RTStrToUtf16(&aszDisk[0], &pwszDisk);
                        if (RT_SUCCESS(rc))
                        {
                            PRTUTF16 pwszTargetNt = NULL;

                            rc = hbdMgrQueryNtLinkTarget(pwszDisk, &pwszTargetNt);
                            if (RT_SUCCESS(rc))
                            {
                                if (cVolumes == cVolumesMax)
                                {
                                    /* Increase array of volumes. */
                                    PRTUTF16 *papwszVolumesNew = (PRTUTF16 *)RTMemAllocZ((cVolumesMax + 10) * sizeof(PRTUTF16));
                                    if (papwszVolumesNew)
                                    {
                                        cVolumesMax += 10;
                                        papwszVolumes = papwszVolumesNew;
                                    }
                                    else
                                    {
                                        RTUtf16Free(pwszTargetNt);
                                        rc = VERR_NO_MEMORY;
                                    }
                                }

                                if (RT_SUCCESS(rc))
                                {
                                    Assert(cVolumes < cVolumesMax);
                                    papwszVolumes[cVolumes++] = pwszTargetNt;
                                    iPart++;
                                }
                            }
                            else if (rc == VERR_FILE_NOT_FOUND)
                            {
                                /* The partition does not exist, so stop trying. */
                                rc = VINF_SUCCESS;
                                break;
                            }

                            RTUtf16Free(pwszDisk);
                        }
                    }
                    else
                        rc = VERR_BUFFER_OVERFLOW;

                } while (RT_SUCCESS(rc));
            }
        }
        else
            rc = VERR_INVALID_STATE;

        RTStrFree(pszDiskNt);
    }

    if (RT_SUCCESS(rc))
    {
        *pcVolumes = cVolumes;
        *ppapwszVolumes = papwszVolumes;
        LogFlowFunc(("rc=%Rrc cVolumes=%u ppapwszVolumes=%p\n", rc, cVolumes, papwszVolumes));
    }
    else
    {
        for (unsigned i = 0; i < cVolumes; i++)
            RTUtf16Free(papwszVolumes[i]);

        RTMemFree(papwszVolumes);
    }

    return rc;
}

static NTSTATUS hbdMgrNtCreateFileWrapper(PRTUTF16 pwszVolume, HANDLE *phVolume)
{
    HANDLE          hVolume = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK Ios     = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    UNICODE_STRING  NtName;

    NtName.Buffer        = (PWSTR)pwszVolume;
    NtName.Length        = (USHORT)(RTUtf16Len(pwszVolume) * sizeof(RTUTF16));
    NtName.MaximumLength = NtName.Length + sizeof(WCHAR);

    OBJECT_ATTRIBUTES ObjAttr;
    InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);

    NTSTATUS rcNt = NtCreateFile(&hVolume,
                                 FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                                 &ObjAttr,
                                 &Ios,
                                 NULL /* Allocation Size*/,
                                 FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_OPEN,
                                 FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL /*EaBuffer*/,
                                 0 /*EaLength*/);
    if (NT_SUCCESS(rcNt))
        rcNt = Ios.Status;

    if (NT_SUCCESS(rcNt))
        *phVolume = hVolume;

    return rcNt;
}

DECLHIDDEN(int) HBDMgrCreate(PHBDMGR phHbdMgr)
{
    AssertPtrReturn(phHbdMgr, VERR_INVALID_POINTER);

    PHBDMGRINT pThis = (PHBDMGRINT)RTMemAllocZ(sizeof(HBDMGRINT));
    if (RT_UNLIKELY(!pThis))
        return VERR_NO_MEMORY;

    int rc = VINF_SUCCESS;
    RTListInit(&pThis->ListClaimed);

    rc = RTSemFastMutexCreate(&pThis->hMtxList);
    if (RT_SUCCESS(rc))
    {
        *phHbdMgr = pThis;
        return VINF_SUCCESS;
    }

    RTMemFree(pThis);
    return rc;
}

DECLHIDDEN(void) HBDMgrDestroy(HBDMGR hHbdMgr)
{
    PHBDMGRINT pThis = hHbdMgr;
    AssertPtrReturnVoid(pThis);

    /* Go through all claimed block devices and release them. */
    RTSemFastMutexRequest(pThis->hMtxList);
    PHBDMGRDEV pIt, pItNext;
    RTListForEachSafe(&pThis->ListClaimed, pIt, pItNext, HBDMGRDEV, ListNode)
    {
        hbdMgrDevUnclaim(pIt);
    }
    RTSemFastMutexRelease(pThis->hMtxList);

    RTSemFastMutexDestroy(pThis->hMtxList);
    RTMemFree(pThis);
}

DECLHIDDEN(bool) HBDMgrIsBlockDevice(const char *pszFilename)
{
    bool fIsBlockDevice = RTStrNICmp(pszFilename, "\\\\.\\PhysicalDrive", sizeof("\\\\.\\PhysicalDrive") - 1) == 0 ? true : false;
    if (!fIsBlockDevice)
        fIsBlockDevice = RTStrNICmp(pszFilename, "\\\\.\\Harddisk", sizeof("\\\\.\\Harddisk") - 1) == 0 ? true : false;

    LogFlowFunc(("returns %s -> %RTbool\n", pszFilename, fIsBlockDevice));
    return fIsBlockDevice;
}

DECLHIDDEN(int) HBDMgrClaimBlockDevice(HBDMGR hHbdMgr, const char *pszFilename)
{
    PHBDMGRINT pThis = hHbdMgr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(HBDMgrIsBlockDevice(pszFilename), VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    PHBDMGRDEV pDev = hbdMgrDevFindByName(pThis, pszFilename);
    if (!pDev)
    {
        PRTUTF16 pwszVolume = NULL;

        rc = RTStrToUtf16(pszFilename, &pwszVolume);
        if (RT_SUCCESS(rc))
        {
            PRTUTF16 pwszVolNt = NULL;

            rc = hbdMgrQueryNtName(pwszVolume, &pwszVolNt);
            if (RT_SUCCESS(rc))
            {
                PRTUTF16 *papwszVolumes = NULL;
                unsigned cVolumes = 0;

                /* Complete disks need to be handled differently. */
                if (!RTStrNCmp(pszFilename, "\\\\.\\PhysicalDrive", sizeof("\\\\.\\PhysicalDrive") - 1))
                {
                    rc = hbdMgrQueryAllMountpointsForDisk(pwszVolNt, &papwszVolumes, &cVolumes);
                    RTUtf16Free(pwszVolNt);
                }
                else
                {
                    papwszVolumes = &pwszVolNt;
                    cVolumes = 1;
                }

                if (RT_SUCCESS(rc))
                {
#ifdef LOG_ENABLED
                    for (unsigned i = 0; i < cVolumes; i++)
                        LogFlowFunc(("Volume %u: %ls\n", i, papwszVolumes[i]));
#endif
                    pDev = (PHBDMGRDEV)RTMemAllocZ(RT_UOFFSETOF_DYN(HBDMGRDEV, ahVolumes[cVolumes]));
                    if (pDev)
                    {
                        pDev->cVolumes = 0;
                        pDev->pszDevice = RTStrDup(pszFilename);
                        if (pDev->pszDevice)
                        {
                            for (unsigned i = 0; i < cVolumes; i++)
                            {
                                HANDLE hVolume;

                                NTSTATUS rcNt = hbdMgrNtCreateFileWrapper(papwszVolumes[i], &hVolume);
                                if (NT_SUCCESS(rcNt))
                                {
                                    DWORD dwReturned = 0;

                                    Assert(hVolume != INVALID_HANDLE_VALUE);
                                    BOOL bRet = DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dwReturned, NULL);
                                    if (bRet)
                                    {
                                         bRet = DeviceIoControl(hVolume, IOCTL_VOLUME_OFFLINE, NULL, 0, NULL, 0, &dwReturned, NULL);
                                         if (bRet)
                                            pDev->ahVolumes[pDev->cVolumes++] = hVolume;
                                         else
                                            rc = RTErrConvertFromWin32(GetLastError());
                                    }
                                    else
                                        rc = RTErrConvertFromWin32(GetLastError());

                                    if (RT_FAILURE(rc))
                                        CloseHandle(hVolume);
                                }
                                else
                                    rc = RTErrConvertFromNtStatus(rcNt);
                            }
                        }
                        else
                            rc = VERR_NO_STR_MEMORY;

                        if (RT_SUCCESS(rc))
                        {
                            RTSemFastMutexRequest(pThis->hMtxList);
                            RTListAppend(&pThis->ListClaimed, &pDev->ListNode);
                            RTSemFastMutexRelease(pThis->hMtxList);
                        }
                        else
                        {
                            /* Close all open handles and take the volumes online again. */
                            for (unsigned i = 0; i < pDev->cVolumes; i++)
                            {
                                DWORD dwReturned = 0;
                                BOOL bRet = DeviceIoControl(pDev->ahVolumes[i], IOCTL_VOLUME_ONLINE, NULL, 0, NULL, 0, &dwReturned, NULL);
                                if (!bRet)
                                    LogRel(("HBDMgmt: Failed to take claimed volume online during cleanup: %s{%Rrc}\n",
                                            pDev->pszDevice, RTErrConvertFromWin32(GetLastError())));

                                CloseHandle(pDev->ahVolumes[i]);
                            }
                            if (pDev->pszDevice)
                                RTStrFree(pDev->pszDevice);
                            RTMemFree(pDev);
                        }
                    }
                    else
                        rc = VERR_NO_MEMORY;

                    for (unsigned i = 0; i < cVolumes; i++)
                        RTUtf16Free(papwszVolumes[i]);

                    RTMemFree(papwszVolumes);
                }
            }

            RTUtf16Free(pwszVolume);
        }
    }
    else
        rc = VERR_ALREADY_EXISTS;

    return rc;
}

DECLHIDDEN(int) HBDMgrUnclaimBlockDevice(HBDMGR hHbdMgr, const char *pszFilename)
{
    PHBDMGRINT pThis = hHbdMgr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    RTSemFastMutexRequest(pThis->hMtxList);
    int rc = VINF_SUCCESS;
    PHBDMGRDEV pDev = hbdMgrDevFindByName(pThis, pszFilename);
    if (pDev)
        hbdMgrDevUnclaim(pDev);
    else
        rc = VERR_NOT_FOUND;
    RTSemFastMutexRelease(pThis->hMtxList);

    return rc;
}

DECLHIDDEN(bool) HBDMgrIsBlockDeviceClaimed(HBDMGR hHbdMgr, const char *pszFilename)
{
    PHBDMGRINT pThis = hHbdMgr;
    AssertPtrReturn(pThis, false);

    RTSemFastMutexRequest(pThis->hMtxList);
    PHBDMGRDEV pIt = hbdMgrDevFindByName(pThis, pszFilename);
    RTSemFastMutexRelease(pThis->hMtxList);

    return pIt ? true : false;
}

