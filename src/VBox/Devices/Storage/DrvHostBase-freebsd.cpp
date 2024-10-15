/* $Id: DrvHostBase-freebsd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvHostBase - Host base drive access driver, FreeBSD specifics.
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
#include <sys/cdefs.h>
#include <sys/param.h>
#include <errno.h>
#include <stdio.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_pass.h>
#include <VBox/err.h>

#include <VBox/scsi.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Host backend specific data.
 */
typedef struct DRVHOSTBASEOS
{
    /** The filehandle of the device. */
    RTFILE                  hFileDevice;
    /** The block size. Set when querying the media size. */
    uint32_t                cbBlock;
    /** SCSI bus number. */
    path_id_t               ScsiBus;
    /** target ID of the passthrough device. */
    target_id_t             ScsiTargetID;
    /** LUN of the passthrough device. */
    lun_id_t                ScsiLunID;
} DRVHOSTBASEOS;
/** Pointer to the host backend specific data. */
typedef DRVHOSTBASEOS *PDRVHOSBASEOS;
AssertCompile(sizeof(DRVHOSTBASEOS) <= 64);

#define DRVHOSTBASE_OS_INT_DECLARED
#include "DrvHostBase.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum buffer size supported by the CAM subsystem. */
#define FBSD_SCSI_MAX_BUFFER_SIZE (64 * _1K)



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
    Assert(cbCmd <= 16 && cbCmd >= 1);
    const uint32_t cbBuf = pcbBuf ? *pcbBuf : 0;
    if (pcbBuf)
        *pcbBuf = 0;

    int rc = VINF_SUCCESS;
    int rcBSD = 0;
    union ccb DeviceCCB;
    union ccb *pDeviceCCB = &DeviceCCB;
    u_int32_t fFlags;

    memset(pDeviceCCB, 0, sizeof(DeviceCCB));
    pDeviceCCB->ccb_h.path_id   = pThis->Os.ScsiBus;
    pDeviceCCB->ccb_h.target_id = pThis->Os.ScsiTargetID;
    pDeviceCCB->ccb_h.target_lun = pThis->Os.ScsiLunID;

    /* The SCSI INQUIRY command can't be passed through directly. */
    if (pbCmd[0] == SCSI_INQUIRY)
    {
        pDeviceCCB->ccb_h.func_code = XPT_GDEV_TYPE;

        rcBSD = ioctl(RTFileToNative(pThis->Os.hFileDevice), CAMIOCOMMAND, pDeviceCCB);
        if (!rcBSD)
        {
            uint32_t cbCopy =   cbBuf < sizeof(struct scsi_inquiry_data)
                              ? cbBuf
                              : sizeof(struct scsi_inquiry_data);;
            memcpy(pvBuf, &pDeviceCCB->cgd.inq_data, cbCopy);
            memset(pbSense, 0, cbSense);

            if (pcbBuf)
                *pcbBuf = cbCopy;
        }
        else
            rc = RTErrConvertFromErrno(errno);
    }
    else
    {
        /* Copy the CDB. */
        memcpy(&pDeviceCCB->csio.cdb_io.cdb_bytes, pbCmd, cbCmd);

        /* Set direction. */
        if (enmTxDir == PDMMEDIATXDIR_NONE)
            fFlags = CAM_DIR_NONE;
        else if (enmTxDir == PDMMEDIATXDIR_FROM_DEVICE)
            fFlags = CAM_DIR_IN;
        else
            fFlags = CAM_DIR_OUT;

        fFlags |= CAM_DEV_QFRZDIS;

        cam_fill_csio(&pDeviceCCB->csio, 1, NULL, fFlags, MSG_SIMPLE_Q_TAG,
                      (u_int8_t *)pvBuf, cbBuf, cbSense, cbCmd,
                      cTimeoutMillies ? cTimeoutMillies : 30000/* timeout */);

        /* Send command */
        rcBSD = ioctl(RTFileToNative(pThis->Os.hFileDevice), CAMIOCOMMAND, pDeviceCCB);
        if (!rcBSD)
        {
            switch (pDeviceCCB->ccb_h.status & CAM_STATUS_MASK)
            {
                case CAM_REQ_CMP:
                    rc = VINF_SUCCESS;
                    break;
                case CAM_SEL_TIMEOUT:
                    rc = VERR_DEV_IO_ERROR;
                    break;
                case CAM_CMD_TIMEOUT:
                    rc = VERR_TIMEOUT;
                    break;
                default:
                    rc = VERR_DEV_IO_ERROR;
            }

            if (pcbBuf)
                *pcbBuf = cbBuf - pDeviceCCB->csio.resid;

            if (pbSense)
                memcpy(pbSense, &pDeviceCCB->csio.sense_data,
                       cbSense - pDeviceCCB->csio.sense_resid);
        }
        else
            rc = RTErrConvertFromErrno(errno);
    }

    return rc;
}


DECLHIDDEN(size_t) drvHostBaseScsiCmdGetBufLimitOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);

    return FBSD_SCSI_MAX_BUFFER_SIZE;
}


DECLHIDDEN(int) drvHostBaseGetMediaSizeOs(PDRVHOSTBASE pThis, uint64_t *pcb)
{
    /*
     * Try a READ_CAPACITY command...
     */
    struct
    {
        uint32_t cBlocks;
        uint32_t cbBlock;
    }           Buf = {0, 0};
    uint32_t    cbBuf = sizeof(Buf);
    uint8_t     abCmd[16] =
    {
        SCSI_READ_CAPACITY, 0, 0, 0, 0, 0, 0,
        0,0,0,0,0,0,0,0,0
    };
    int rc = drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_FROM_DEVICE, &Buf, &cbBuf, NULL, 0, 0);
    if (RT_SUCCESS(rc))
    {
        Assert(cbBuf == sizeof(Buf));
        Buf.cBlocks = RT_BE2H_U32(Buf.cBlocks);
        Buf.cbBlock = RT_BE2H_U32(Buf.cbBlock);
        //if (Buf.cbBlock > 2048) /* everyone else is doing this... check if it needed/right.*/
        //    Buf.cbBlock = 2048;
        pThis->Os.cbBlock = Buf.cbBlock;

        *pcb = (uint64_t)Buf.cBlocks * Buf.cbBlock;
    }
    return rc;
}


DECLHIDDEN(int) drvHostBaseReadOs(PDRVHOSTBASE pThis, uint64_t off, void *pvBuf, size_t cbRead)
{
    int rc = VINF_SUCCESS;

    if (pThis->Os.cbBlock)
    {
        /*
         * Issue a READ(12) request.
         */
        do
        {
            const uint32_t  LBA       = off / pThis->Os.cbBlock;
            AssertReturn(!(off % pThis->Os.cbBlock), VERR_INVALID_PARAMETER);
            uint32_t        cbRead32  =   cbRead > FBSD_SCSI_MAX_BUFFER_SIZE
                                        ? FBSD_SCSI_MAX_BUFFER_SIZE
                                        : (uint32_t)cbRead;
            const uint32_t  cBlocks   = cbRead32 / pThis->Os.cbBlock;
            AssertReturn(!(cbRead % pThis->Os.cbBlock), VERR_INVALID_PARAMETER);
            uint8_t         abCmd[16] =
            {
                SCSI_READ_12, 0,
                RT_BYTE4(LBA),     RT_BYTE3(LBA),     RT_BYTE2(LBA),     RT_BYTE1(LBA),
                RT_BYTE4(cBlocks), RT_BYTE3(cBlocks), RT_BYTE2(cBlocks), RT_BYTE1(cBlocks),
                0, 0, 0, 0, 0
            };
            rc = drvHostBaseScsiCmdOs(pThis, abCmd, 12, PDMMEDIATXDIR_FROM_DEVICE, pvBuf, &cbRead32, NULL, 0, 0);

            off    += cbRead32;
            cbRead -= cbRead32;
            pvBuf   = (uint8_t *)pvBuf + cbRead32;
        } while ((cbRead > 0) && RT_SUCCESS(rc));
    }
    else
        rc = VERR_MEDIA_NOT_PRESENT;

    return rc;
}


DECLHIDDEN(int) drvHostBaseWriteOs(PDRVHOSTBASE pThis, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    RT_NOREF4(pThis, off, pvBuf, cbWrite);
    return VERR_WRITE_PROTECT;
}


DECLHIDDEN(int) drvHostBaseFlushOs(PDRVHOSTBASE pThis)
{
    RT_NOREF1(pThis);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) drvHostBaseDoLockOs(PDRVHOSTBASE pThis, bool fLock)
{
    uint8_t abCmd[16] =
    {
        SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL, 0, 0, 0, fLock, 0,
        0,0,0,0,0,0,0,0,0,0
    };
    return drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_NONE, NULL, NULL, NULL, 0, 0);
}


DECLHIDDEN(int) drvHostBaseEjectOs(PDRVHOSTBASE pThis)
{
    uint8_t abCmd[16] =
    {
        SCSI_START_STOP_UNIT, 0, 0, 0, 2 /*eject+stop*/, 0,
        0,0,0,0,0,0,0,0,0,0
    };
    return drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_NONE, NULL, NULL, NULL, 0, 0);
}


DECLHIDDEN(int) drvHostBaseQueryMediaStatusOs(PDRVHOSTBASE pThis, bool *pfMediaChanged, bool *pfMediaPresent)
{
    /*
     * Issue a TEST UNIT READY request.
     */
    *pfMediaChanged = false;
    *pfMediaPresent = false;
    uint8_t abCmd[16] = { SCSI_TEST_UNIT_READY, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
    uint8_t abSense[32];
    int rc = drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_NONE, NULL, NULL, abSense, sizeof(abSense), 0);
    if (RT_SUCCESS(rc))
        *pfMediaPresent = true;
    else if (   rc == VERR_UNRESOLVED_ERROR
             && abSense[2] == 6 /* unit attention */
             && (   (abSense[12] == 0x29 && abSense[13] < 5 /* reset */)
                 || (abSense[12] == 0x2a && abSense[13] == 0 /* parameters changed */)                        //???
                 || (abSense[12] == 0x3f && abSense[13] == 0 /* target operating conditions have changed */)  //???
                 || (abSense[12] == 0x3f && abSense[13] == 2 /* changed operating definition */)              //???
                 || (abSense[12] == 0x3f && abSense[13] == 3 /* inquiry parameters changed */)
                 || (abSense[12] == 0x3f && abSense[13] == 5 /* device identifier changed */)
                 )
            )
    {
        *pfMediaPresent = false;
        *pfMediaChanged = true;
        rc = VINF_SUCCESS;
        /** @todo check this media change stuff on Darwin. */
    }

    return rc;
}


DECLHIDDEN(void) drvHostBaseInitOs(PDRVHOSTBASE pThis)
{
    pThis->Os.hFileDevice = NIL_RTFILE;
}


DECLHIDDEN(int) drvHostBaseOpenOs(PDRVHOSTBASE pThis, bool fReadOnly)
{
    RT_NOREF(fReadOnly);
    RTFILE hFileDevice;
    int rc = RTFileOpen(&hFileDevice, pThis->pszDevice, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * The current device handle can't passthrough SCSI commands.
     * We have to get he passthrough device path and open this.
     */
    union ccb DeviceCCB;
    memset(&DeviceCCB, 0, sizeof(DeviceCCB));

    DeviceCCB.ccb_h.func_code = XPT_GDEVLIST;
    int rcBSD = ioctl(RTFileToNative(hFileDevice), CAMGETPASSTHRU, &DeviceCCB);
    if (!rcBSD)
    {
        char *pszPassthroughDevice = NULL;
        rc = RTStrAPrintf(&pszPassthroughDevice, "/dev/%s%u",
                          DeviceCCB.cgdl.periph_name, DeviceCCB.cgdl.unit_number);
        if (rc >= 0)
        {
            RTFILE hPassthroughDevice;
            rc = RTFileOpen(&hPassthroughDevice, pszPassthroughDevice, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
            RTStrFree(pszPassthroughDevice);
            if (RT_SUCCESS(rc))
            {
                /* Get needed device parameters. */

                /*
                 * The device path, target id and lun id. Those are
                 * needed for the SCSI passthrough ioctl.
                 */
                memset(&DeviceCCB, 0, sizeof(DeviceCCB));
                DeviceCCB.ccb_h.func_code = XPT_GDEVLIST;

                rcBSD = ioctl(RTFileToNative(hPassthroughDevice), CAMGETPASSTHRU, &DeviceCCB);
                if (!rcBSD)
                {
                    if (DeviceCCB.cgdl.status != CAM_GDEVLIST_ERROR)
                    {
                        pThis->Os.ScsiBus      = DeviceCCB.ccb_h.path_id;
                        pThis->Os.ScsiTargetID = DeviceCCB.ccb_h.target_id;
                        pThis->Os.ScsiLunID    = DeviceCCB.ccb_h.target_lun;
                        pThis->Os.hFileDevice  = hPassthroughDevice;
                    }
                    else
                    {
                        /* The passthrough device wasn't found. */
                        rc = VERR_NOT_FOUND;
                    }
                }
                else
                    rc = RTErrConvertFromErrno(errno);

                if (RT_FAILURE(rc))
                    RTFileClose(hPassthroughDevice);
            }
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    else
        rc = RTErrConvertFromErrno(errno);

    RTFileClose(hFileDevice);
    return rc;
}


DECLHIDDEN(int) drvHostBaseMediaRefreshOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


DECLHIDDEN(bool) drvHostBaseIsMediaPollingRequiredOs(PDRVHOSTBASE pThis)
{
    if (pThis->enmType == PDMMEDIATYPE_CDROM || pThis->enmType == PDMMEDIATYPE_DVD)
        return true;

    AssertMsgFailed(("FreeBSD supports only CD/DVD host drive access\n"));
    return false;
}


DECLHIDDEN(void) drvHostBaseDestructOs(PDRVHOSTBASE pThis)
{
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

    if (pThis->Os.hFileDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->Os.hFileDevice);
        AssertRC(rc);
        pThis->Os.hFileDevice = NIL_RTFILE;
    }
}

