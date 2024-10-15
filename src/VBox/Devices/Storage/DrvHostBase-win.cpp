/* $Id: DrvHostBase-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvHostBase - Host base drive access driver, Windows specifics.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_BASE
#include <iprt/nt/nt-and-windows.h>
#include <dbt.h>
#include <ntddscsi.h>

#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <VBox/err.h>
#include <VBox/scsi.h>

/**
 * Host backend specific data.
 */
typedef struct DRVHOSTBASEOS
{
    /** The filehandle of the device. */
    RTFILE                  hFileDevice;
    /** Handle to the window we use to catch the device change broadcast messages. */
    volatile HWND           hwndDeviceChange;
    /** The unit mask. */
    DWORD                   fUnitMask;
    /** Handle of the poller thread. */
    RTTHREAD                hThrdMediaChange;
} DRVHOSTBASEOS;
/** Pointer to the host backend specific data. */
typedef DRVHOSTBASEOS *PDRVHOSBASEOS;
AssertCompile(sizeof(DRVHOSTBASEOS) <= 64);

#define DRVHOSTBASE_OS_INT_DECLARED
#include "DrvHostBase.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum buffer size we support, check whether darwin has some real upper limit. */
#define WIN_SCSI_MAX_BUFFER_SIZE (100 * _1K)



/**
 * Window procedure for the invisible window used to catch the WM_DEVICECHANGE broadcasts.
 */
static LRESULT CALLBACK DeviceChangeWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Log2(("DeviceChangeWindowProc: hwnd=%08x uMsg=%08x\n", hwnd, uMsg));
    if (uMsg == WM_DESTROY)
    {
        PDRVHOSTBASE pThis = (PDRVHOSTBASE)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pThis)
            ASMAtomicXchgSize(&pThis->Os.hwndDeviceChange, NULL);
        PostQuitMessage(0);
    }

    if (uMsg != WM_DEVICECHANGE)
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

    PDEV_BROADCAST_HDR  lpdb = (PDEV_BROADCAST_HDR)lParam;
    PDRVHOSTBASE        pThis = (PDRVHOSTBASE)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    Assert(pThis);
    if (pThis == NULL)
        return 0;

    switch (wParam)
    {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE:
            // Check whether a CD or DVD was inserted into or removed from a drive.
            if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
            {
                PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
                if (    (lpdbv->dbcv_flags & DBTF_MEDIA)
                    &&  (pThis->Os.fUnitMask & lpdbv->dbcv_unitmask))
                {
                    RTCritSectEnter(&pThis->CritSect);
                    if (wParam == DBT_DEVICEARRIVAL)
                    {
                        int cRetries = 10;
                        int rc = DRVHostBaseMediaPresent(pThis);
                        while (RT_FAILURE(rc) && cRetries-- > 0)
                        {
                            RTThreadSleep(50);
                            rc = DRVHostBaseMediaPresent(pThis);
                        }
                    }
                    else
                        DRVHostBaseMediaNotPresent(pThis);
                    RTCritSectLeave(&pThis->CritSect);
                }
            }
            break;
    }
    return TRUE;
}


/**
 * This thread will wait for changed media notificatons.
 *
 * @returns Ignored.
 * @param   ThreadSelf  Handle of this thread. Ignored.
 * @param   pvUser      Pointer to the driver instance structure.
 */
static DECLCALLBACK(int) drvHostBaseMediaThreadWin(RTTHREAD ThreadSelf, void *pvUser)
{
    PDRVHOSTBASE pThis = (PDRVHOSTBASE)pvUser;
    LogFlow(("%s-%d: drvHostBaseMediaThreadWin: ThreadSelf=%p pvUser=%p\n",
             pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, ThreadSelf, pvUser));
    static WNDCLASS s_classDeviceChange = {0};
    static ATOM     s_hAtomDeviceChange = 0;

    /*
     * Register custom window class.
     */
    if (s_hAtomDeviceChange == 0)
    {
        memset(&s_classDeviceChange, 0, sizeof(s_classDeviceChange));
        s_classDeviceChange.lpfnWndProc   = DeviceChangeWindowProc;
        s_classDeviceChange.lpszClassName = "VBOX_DeviceChangeClass";
        s_classDeviceChange.hInstance     = GetModuleHandle("VBoxDD.dll");
        Assert(s_classDeviceChange.hInstance);
        s_hAtomDeviceChange = RegisterClassA(&s_classDeviceChange);
        Assert(s_hAtomDeviceChange);
    }

    /*
     * Create Window w/ the pThis as user data.
     */
    HWND hwnd = CreateWindow((LPCTSTR)s_hAtomDeviceChange, "", WS_POPUP, 0, 0, 0, 0, 0, 0, s_classDeviceChange.hInstance, 0);
    AssertMsg(hwnd, ("CreateWindow failed with %d\n", GetLastError()));
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

    /*
     * Signal the waiting EMT thread that everything went fine.
     */
    ASMAtomicXchgPtr((void * volatile *)&pThis->Os.hwndDeviceChange, hwnd);
    RTThreadUserSignal(ThreadSelf);
    if (!hwnd)
    {
        LogFlow(("%s-%d: drvHostBaseMediaThreadWin: returns VERR_GENERAL_FAILURE\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));
        return VERR_GENERAL_FAILURE;
    }
    LogFlow(("%s-%d: drvHostBaseMediaThreadWin: Created hwndDeviceChange=%p\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, hwnd));

    /*
     * Message pump.
     */
    MSG         Msg;
    BOOL        fRet;
    while ((fRet = GetMessage(&Msg, NULL, 0, 0)) != FALSE)
    {
        if (fRet != -1)
        {
            TranslateMessage(&Msg);
            DispatchMessage(&Msg);
        }
        //else: handle the error and possibly exit
    }
    Assert(!pThis->Os.hwndDeviceChange);
    /* (Don't clear the thread handle here, the destructor thread is using it to wait.) */
    LogFlow(("%s-%d: drvHostBaseMediaThreadWin: returns VINF_SUCCESS\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));
    return VINF_SUCCESS;
}


DECLHIDDEN(int) drvHostBaseScsiCmdOs(PDRVHOSTBASE pThis, const uint8_t *pbCmd, size_t cbCmd, PDMMEDIATXDIR enmTxDir,
                                     void *pvBuf, uint32_t *pcbBuf, uint8_t *pbSense, size_t cbSense, uint32_t cTimeoutMillies)
{
    /*
     * Minimal input validation.
     */
    Assert(enmTxDir == PDMMEDIATXDIR_NONE || enmTxDir == PDMMEDIATXDIR_FROM_DEVICE || enmTxDir == PDMMEDIATXDIR_TO_DEVICE);
    Assert(!pvBuf || pcbBuf);
    Assert(pvBuf || enmTxDir == PDMMEDIATXDIR_NONE);
    Assert(pbSense || !cbSense);
    AssertPtr(pbCmd);
    Assert(cbCmd <= 16 && cbCmd >= 1); RT_NOREF(cbCmd);

    int rc = VERR_GENERAL_FAILURE;
    int direction;
    struct _REQ
    {
        SCSI_PASS_THROUGH_DIRECT spt;
        uint8_t aSense[64];
    } Req;
    DWORD cbReturned = 0;

    switch (enmTxDir)
    {
        case PDMMEDIATXDIR_NONE:
            direction = SCSI_IOCTL_DATA_UNSPECIFIED;
            break;
        case PDMMEDIATXDIR_FROM_DEVICE:
            Assert(*pcbBuf != 0);
            /* Make sure that the buffer is clear for commands reading
             * data. The actually received data may be shorter than what
             * we expect, and due to the unreliable feedback about how much
             * data the ioctl actually transferred, it's impossible to
             * prevent that. Returning previous buffer contents may cause
             * security problems inside the guest OS, if users can issue
             * commands to the CDROM device. */
            memset(pvBuf, '\0', *pcbBuf);
            direction = SCSI_IOCTL_DATA_IN;
            break;
        case PDMMEDIATXDIR_TO_DEVICE:
            direction = SCSI_IOCTL_DATA_OUT;
            break;
        default:
            AssertMsgFailed(("enmTxDir invalid!\n"));
            direction = SCSI_IOCTL_DATA_UNSPECIFIED;
    }
    memset(&Req, '\0', sizeof(Req));
    Req.spt.Length = sizeof(Req.spt);
    Req.spt.CdbLength = 12;
    memcpy(Req.spt.Cdb, pbCmd, Req.spt.CdbLength);
    Req.spt.DataBuffer = pvBuf;
    Req.spt.DataTransferLength = *pcbBuf;
    Req.spt.DataIn = direction;
    Req.spt.TimeOutValue = (cTimeoutMillies + 999) / 1000; /* Convert to seconds */
    Assert(cbSense <= sizeof(Req.aSense));
    Req.spt.SenseInfoLength = (UCHAR)RT_MIN(sizeof(Req.aSense), cbSense);
    Req.spt.SenseInfoOffset = RT_UOFFSETOF(struct _REQ, aSense);
    if (DeviceIoControl((HANDLE)RTFileToNative(pThis->Os.hFileDevice), IOCTL_SCSI_PASS_THROUGH_DIRECT,
                        &Req, sizeof(Req), &Req, sizeof(Req), &cbReturned, NULL))
    {
        if (cbReturned > RT_UOFFSETOF(struct _REQ, aSense))
            memcpy(pbSense, Req.aSense, cbSense);
        else
            memset(pbSense, '\0', cbSense);
        /* Windows shares the property of not properly reflecting the actually
         * transferred data size. See above. Assume that everything worked ok.
         * Except if there are sense information. */
        rc = (pbSense[2] & 0x0f) == SCSI_SENSE_NONE
                 ? VINF_SUCCESS
                 : VERR_DEV_IO_ERROR;
    }
    else
        rc = RTErrConvertFromWin32(GetLastError());
    Log2(("%s: scsistatus=%d bytes returned=%d tlength=%d\n", __FUNCTION__, Req.spt.ScsiStatus, cbReturned, Req.spt.DataTransferLength));

    return rc;
}


DECLHIDDEN(size_t) drvHostBaseScsiCmdGetBufLimitOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);

    return WIN_SCSI_MAX_BUFFER_SIZE;
}


DECLHIDDEN(int) drvHostBaseGetMediaSizeOs(PDRVHOSTBASE pThis, uint64_t *pcb)
{
    int rc = VERR_GENERAL_FAILURE;

    if (PDMMEDIATYPE_IS_FLOPPY(pThis->enmType))
    {
        DISK_GEOMETRY   geom;
        DWORD           cbBytesReturned;
        int             cbSectors;

        memset(&geom, 0, sizeof(geom));
        rc = DeviceIoControl((HANDLE)RTFileToNative(pThis->Os.hFileDevice), IOCTL_DISK_GET_DRIVE_GEOMETRY,
                             NULL, 0, &geom, sizeof(geom), &cbBytesReturned,  NULL);
        if (rc) {
            cbSectors = geom.Cylinders.QuadPart * geom.TracksPerCylinder * geom.SectorsPerTrack;
            *pcb = cbSectors * geom.BytesPerSector;
            rc = VINF_SUCCESS;
        }
        else
        {
            DWORD   dwLastError;

            dwLastError = GetLastError();
            rc = RTErrConvertFromWin32(dwLastError);
            Log(("DrvHostFloppy: IOCTL_DISK_GET_DRIVE_GEOMETRY(%s) failed, LastError=%d rc=%Rrc\n",
                 pThis->pszDevice, dwLastError, rc));
            return rc;
        }
    }
    else
    {
        /* use NT api, retry a few times if the media is being verified. */
        IO_STATUS_BLOCK             IoStatusBlock = {0};
        FILE_FS_SIZE_INFORMATION    FsSize = {{0}};
        NTSTATUS rcNt = NtQueryVolumeInformationFile((HANDLE)RTFileToNative(pThis->Os.hFileDevice),  &IoStatusBlock,
                                                     &FsSize, sizeof(FsSize), FileFsSizeInformation);
        int cRetries = 5;
        while (rcNt == STATUS_VERIFY_REQUIRED && cRetries-- > 0)
        {
            RTThreadSleep(10);
            rcNt = NtQueryVolumeInformationFile((HANDLE)RTFileToNative(pThis->Os.hFileDevice),  &IoStatusBlock,
                                                &FsSize, sizeof(FsSize), FileFsSizeInformation);
        }
        if (rcNt >= 0)
        {
            *pcb = FsSize.TotalAllocationUnits.QuadPart * FsSize.BytesPerSector;
            return VINF_SUCCESS;
        }

        /* convert nt status code to VBox status code. */
        /** @todo Make conversion function!. */
        switch (rcNt)
        {
            case STATUS_NO_MEDIA_IN_DEVICE:     rc = VERR_MEDIA_NOT_PRESENT; break;
            case STATUS_VERIFY_REQUIRED:        rc = VERR_TRY_AGAIN; break;
        }
        LogFlow(("drvHostBaseGetMediaSize: NtQueryVolumeInformationFile -> %#lx\n", rcNt, rc));
    }
    return rc;
}


DECLHIDDEN(int) drvHostBaseReadOs(PDRVHOSTBASE pThis, uint64_t off, void *pvBuf, size_t cbRead)
{
    return RTFileReadAt(pThis->Os.hFileDevice, off, pvBuf, cbRead, NULL);
}


DECLHIDDEN(int) drvHostBaseWriteOs(PDRVHOSTBASE pThis, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    return RTFileWriteAt(pThis->Os.hFileDevice, off, pvBuf, cbWrite, NULL);
}


DECLHIDDEN(int) drvHostBaseFlushOs(PDRVHOSTBASE pThis)
{
    return RTFileFlush(pThis->Os.hFileDevice);
}


DECLHIDDEN(int) drvHostBaseDoLockOs(PDRVHOSTBASE pThis, bool fLock)
{
    PREVENT_MEDIA_REMOVAL PreventMediaRemoval = {fLock};
    DWORD cbReturned;
    int rc;
    if (DeviceIoControl((HANDLE)RTFileToNative(pThis->Os.hFileDevice), IOCTL_STORAGE_MEDIA_REMOVAL,
                        &PreventMediaRemoval, sizeof(PreventMediaRemoval),
                        NULL, 0, &cbReturned,
                        NULL))
        rc = VINF_SUCCESS;
    else
        /** @todo figure out the return codes for already locked. */
        rc = RTErrConvertFromWin32(GetLastError());

    return rc;
}


DECLHIDDEN(int) drvHostBaseEjectOs(PDRVHOSTBASE pThis)
{
    int rc = VINF_SUCCESS;
    RTFILE hFileDevice = pThis->Os.hFileDevice;
    if (hFileDevice == NIL_RTFILE) /* obsolete crap */
        rc = RTFileOpen(&hFileDevice, pThis->pszDeviceOpen, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        /* do ioctl */
        DWORD cbReturned;
        if (DeviceIoControl((HANDLE)RTFileToNative(hFileDevice), IOCTL_STORAGE_EJECT_MEDIA,
                            NULL, 0,
                            NULL, 0, &cbReturned,
                            NULL))
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromWin32(GetLastError());

        /* clean up handle */
        if (hFileDevice != pThis->Os.hFileDevice)
            RTFileClose(hFileDevice);
    }
    else
        AssertMsgFailed(("Failed to open '%s' for ejecting this tray.\n",  rc));

    return rc;
}


DECLHIDDEN(void) drvHostBaseInitOs(PDRVHOSTBASE pThis)
{
    pThis->Os.hFileDevice      = NIL_RTFILE;
    pThis->Os.hwndDeviceChange = NULL;
    pThis->Os.hThrdMediaChange = NIL_RTTHREAD;
}


DECLHIDDEN(int) drvHostBaseOpenOs(PDRVHOSTBASE pThis, bool fReadOnly)
{
    UINT uDriveType = GetDriveType(pThis->pszDevice);
    switch (pThis->enmType)
    {
        case PDMMEDIATYPE_FLOPPY_360:
        case PDMMEDIATYPE_FLOPPY_720:
        case PDMMEDIATYPE_FLOPPY_1_20:
        case PDMMEDIATYPE_FLOPPY_1_44:
        case PDMMEDIATYPE_FLOPPY_2_88:
        case PDMMEDIATYPE_FLOPPY_FAKE_15_6:
        case PDMMEDIATYPE_FLOPPY_FAKE_63_5:
            if (uDriveType != DRIVE_REMOVABLE)
            {
                AssertMsgFailed(("Configuration error: '%s' is not a floppy (type=%d)\n",
                                 pThis->pszDevice, uDriveType));
                return VERR_INVALID_PARAMETER;
            }
            break;
        case PDMMEDIATYPE_CDROM:
        case PDMMEDIATYPE_DVD:
            if (uDriveType != DRIVE_CDROM)
            {
                AssertMsgFailed(("Configuration error: '%s' is not a cdrom (type=%d)\n",
                                 pThis->pszDevice, uDriveType));
                return VERR_INVALID_PARAMETER;
            }
            break;
        case PDMMEDIATYPE_HARD_DISK:
        default:
            AssertMsgFailed(("enmType=%d\n", pThis->enmType));
            return VERR_INVALID_PARAMETER;
    }

    int iBit = RT_C_TO_UPPER(pThis->pszDevice[0]) - 'A';
    if (    iBit > 'Z' - 'A'
        ||  pThis->pszDevice[1] != ':'
        ||  pThis->pszDevice[2])
    {
        AssertMsgFailed(("Configuration error: Invalid drive specification: '%s'\n", pThis->pszDevice));
        return VERR_INVALID_PARAMETER;
    }
    pThis->Os.fUnitMask = 1 << iBit;
    RTStrAPrintf(&pThis->pszDeviceOpen, "\\\\.\\%s", pThis->pszDevice);
    if (!pThis->pszDeviceOpen)
        return VERR_NO_MEMORY;

    uint32_t fFlags = (fReadOnly ? RTFILE_O_READ : RTFILE_O_READWRITE) | RTFILE_O_OPEN | RTFILE_O_DENY_NONE;
    int rc = RTFileOpen(&pThis->Os.hFileDevice, pThis->pszDeviceOpen, fFlags);

    if (RT_SUCCESS(rc))
    {
        /*
         * Start the thread which will wait for the media change events.
         */
        rc = RTThreadCreate(&pThis->Os.hThrdMediaChange, drvHostBaseMediaThreadWin, pThis, 0,
                            RTTHREADTYPE_INFREQUENT_POLLER, RTTHREADFLAGS_WAITABLE, "DVDMEDIA");
        if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("Failed to create poller thread. rc=%Rrc\n", rc));
            return rc;
        }

        /*
         * Wait for the thread to start up (!w32:) and do one detection loop.
         */
        rc = RTThreadUserWait(pThis->Os.hThrdMediaChange, 10000);
        AssertRC(rc);

        if (!pThis->Os.hwndDeviceChange)
            return VERR_GENERAL_FAILURE;

        DRVHostBaseMediaPresent(pThis);
    }

    return rc;
}


DECLHIDDEN(int) drvHostBaseMediaRefreshOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) drvHostBaseQueryMediaStatusOs(PDRVHOSTBASE pThis, bool *pfMediaChanged, bool *pfMediaPresent)
{
    RT_NOREF3(pThis, pfMediaChanged, pfMediaPresent); /* We don't support the polling method. */
    return VERR_NOT_SUPPORTED;
}


DECLHIDDEN(bool) drvHostBaseIsMediaPollingRequiredOs(PDRVHOSTBASE pThis)
{
    /* For Windows we alwys use an internal approach. */
    RT_NOREF(pThis);
    return false;
}


DECLHIDDEN(void) drvHostBaseDestructOs(PDRVHOSTBASE pThis)
{
    /*
     * Terminate the thread.
     */
    if (pThis->Os.hThrdMediaChange != NIL_RTTHREAD)
    {
        int rc;
        int cTimes = 50;
        do
        {
            if (pThis->Os.hwndDeviceChange)
                PostMessage(pThis->Os.hwndDeviceChange, WM_CLOSE, 0, 0); /* default win proc will destroy the window */

            rc = RTThreadWait(pThis->Os.hThrdMediaChange, 100, NULL);
        } while (cTimes-- > 0 && rc == VERR_TIMEOUT);

        if (RT_SUCCESS(rc))
            pThis->Os.hThrdMediaChange = NIL_RTTHREAD;
    }

    /*
     * Unlock the drive if we've locked it or we're in passthru mode.
     */
    if (    pThis->fLocked
        &&  pThis->Os.hFileDevice != NIL_RTFILE
        &&  pThis->pfnDoLock)
    {
        int rc = pThis->pfnDoLock(pThis, false);
        if (RT_SUCCESS(rc))
            pThis->fLocked = false;
    }

    if (pThis->Os.hwndDeviceChange)
    {
        if (SetWindowLongPtr(pThis->Os.hwndDeviceChange, GWLP_USERDATA, 0) == (LONG_PTR)pThis)
            PostMessage(pThis->Os.hwndDeviceChange, WM_CLOSE, 0, 0); /* default win proc will destroy the window */
        pThis->Os.hwndDeviceChange = NULL;
    }

    if (pThis->Os.hFileDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->Os.hFileDevice);
        AssertRC(rc);
        pThis->Os.hFileDevice = NIL_RTFILE;
    }
}

