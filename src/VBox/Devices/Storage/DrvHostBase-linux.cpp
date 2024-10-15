/* $Id: DrvHostBase-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvHostBase - Host base drive access driver, Linux specifics.
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
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <linux/version.h>
/* All the following crap is apparently not necessary anymore since Linux
 * version 2.6.29. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29)
/* This is a hack to work around conflicts between these linux kernel headers
 * and the GLIBC tcpip headers. They have different declarations of the 4
 * standard byte order functions. */
# define _LINUX_BYTEORDER_GENERIC_H
/* This is another hack for not bothering with C++ unfriendly byteswap macros. */
/* Those macros that are needed are defined in the header below. */
# include "swab.h"
#endif
#include <linux/fd.h>
#include <linux/cdrom.h>
#include <limits.h>

#include <iprt/mem.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <VBox/err.h>
#include <VBox/scsi.h>


/**
 * Host backend specific data (needed by DrvHostBase.h).
 */
typedef struct DRVHOSTBASEOS
{
    /** The filehandle of the device. */
    RTFILE                  hFileDevice;
    /** Double buffer required for ioctl with the Linux kernel as long as we use
     * remap_pfn_range() instead of vm_insert_page(). */
    uint8_t                *pbDoubleBuffer;
    /** Previous disk inserted indicator for the media polling on floppy drives. */
    bool                   fPrevDiskIn;
} DRVHOSTBASEOS;
/** Pointer to the host backend specific data. */
typedef DRVHOSTBASEOS *PDRVHOSBASEOS;
AssertCompile(sizeof(DRVHOSTBASEOS) <= 64);

#define DRVHOSTBASE_OS_INT_DECLARED
#include "DrvHostBase.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum buffer size supported by the kernel interface. */
#define LNX_SCSI_MAX_BUFFER_SIZE (100 * _1K)





DECLHIDDEN(int) drvHostBaseScsiCmdOs(PDRVHOSTBASE pThis, const uint8_t *pbCmd, size_t cbCmd, PDMMEDIATXDIR enmTxDir,
                                     void *pvBuf, uint32_t *pcbBuf, uint8_t *pbSense, size_t cbSense, uint32_t cTimeoutMillies)
{
    /*
     * Minimal input validation.
     */
    Assert(enmTxDir == PDMMEDIATXDIR_NONE || enmTxDir == PDMMEDIATXDIR_FROM_DEVICE || enmTxDir == PDMMEDIATXDIR_TO_DEVICE);
    Assert(!pvBuf || pcbBuf);
    Assert(pvBuf || enmTxDir == PDMMEDIATXDIR_NONE);
    Assert(pbSense || !cbSense); RT_NOREF(cbSense);
    AssertPtr(pbCmd);
    Assert(cbCmd <= 16 && cbCmd >= 1);

    /* Allocate the temporary buffer lazily. */
    if(RT_UNLIKELY(!pThis->Os.pbDoubleBuffer))
    {
        pThis->Os.pbDoubleBuffer = (uint8_t *)RTMemAlloc(LNX_SCSI_MAX_BUFFER_SIZE);
        if (!pThis->Os.pbDoubleBuffer)
            return VERR_NO_MEMORY;
    }

    int rc = VERR_GENERAL_FAILURE;
    int direction;
    struct cdrom_generic_command cgc;

    switch (enmTxDir)
    {
        case PDMMEDIATXDIR_NONE:
            Assert(*pcbBuf == 0);
            direction = CGC_DATA_NONE;
            break;
        case PDMMEDIATXDIR_FROM_DEVICE:
            Assert(*pcbBuf != 0);
            Assert(*pcbBuf <= LNX_SCSI_MAX_BUFFER_SIZE);
            /* Make sure that the buffer is clear for commands reading
             * data. The actually received data may be shorter than what
             * we expect, and due to the unreliable feedback about how much
             * data the ioctl actually transferred, it's impossible to
             * prevent that. Returning previous buffer contents may cause
             * security problems inside the guest OS, if users can issue
             * commands to the CDROM device. */
            memset(pThis->Os.pbDoubleBuffer, '\0', *pcbBuf);
            direction = CGC_DATA_READ;
            break;
        case PDMMEDIATXDIR_TO_DEVICE:
            Assert(*pcbBuf != 0);
            Assert(*pcbBuf <= LNX_SCSI_MAX_BUFFER_SIZE);
            memcpy(pThis->Os.pbDoubleBuffer, pvBuf, *pcbBuf);
            direction = CGC_DATA_WRITE;
            break;
        default:
            AssertMsgFailed(("enmTxDir invalid!\n"));
            direction = CGC_DATA_NONE;
    }
    memset(&cgc, '\0', sizeof(cgc));
    memcpy(cgc.cmd, pbCmd, RT_MIN(CDROM_PACKET_SIZE, cbCmd));
    cgc.buffer = (unsigned char *)pThis->Os.pbDoubleBuffer;
    cgc.buflen = *pcbBuf;
    cgc.stat = 0;
    Assert(cbSense >= sizeof(struct request_sense));
    cgc.sense = (struct request_sense *)pbSense;
    cgc.data_direction = direction;
    cgc.quiet = false;
    cgc.timeout = cTimeoutMillies;
    rc = ioctl(RTFileToNative(pThis->Os.hFileDevice), CDROM_SEND_PACKET, &cgc);
    if (rc < 0)
    {
        if (errno == EBUSY)
            rc = VERR_PDM_MEDIA_LOCKED;
        else if (errno == ENOSYS)
            rc = VERR_NOT_SUPPORTED;
        else
        {
            rc = RTErrConvertFromErrno(errno);
            if (rc == VERR_ACCESS_DENIED && cgc.sense->sense_key == SCSI_SENSE_NONE)
                cgc.sense->sense_key = SCSI_SENSE_ILLEGAL_REQUEST;
            Log2(("%s: error status %d, rc=%Rrc\n", __FUNCTION__, cgc.stat, rc));
        }
    }
    switch (enmTxDir)
    {
        case PDMMEDIATXDIR_FROM_DEVICE:
            memcpy(pvBuf, pThis->Os.pbDoubleBuffer, *pcbBuf);
            break;
        default:
            ;
    }
    Log2(("%s: after ioctl: cgc.buflen=%d txlen=%d\n", __FUNCTION__, cgc.buflen, *pcbBuf));
    /* The value of cgc.buflen does not reliably reflect the actual amount
     * of data transferred (for packet commands with little data transfer
     * it's 0). So just assume that everything worked ok. */

    return rc;
}


DECLHIDDEN(size_t) drvHostBaseScsiCmdGetBufLimitOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);

    return LNX_SCSI_MAX_BUFFER_SIZE;
}


DECLHIDDEN(int) drvHostBaseGetMediaSizeOs(PDRVHOSTBASE pThis, uint64_t *pcb)
{
    int rc = VERR_INVALID_STATE;

    if (PDMMEDIATYPE_IS_FLOPPY(pThis->enmType))
    {
        rc = ioctl(RTFileToNative(pThis->Os.hFileDevice), FDFLUSH);
        if (rc)
        {
            rc = RTErrConvertFromErrno (errno);
            Log(("DrvHostFloppy: FDFLUSH ioctl(%s) failed, errno=%d rc=%Rrc\n", pThis->pszDevice, errno, rc));
            return rc;
        }

        floppy_drive_struct DrvStat;
        rc = ioctl(RTFileToNative(pThis->Os.hFileDevice), FDGETDRVSTAT, &DrvStat);
        if (rc)
        {
            rc = RTErrConvertFromErrno(errno);
            Log(("DrvHostFloppy: FDGETDRVSTAT ioctl(%s) failed, errno=%d rc=%Rrc\n", pThis->pszDevice, errno, rc));
            return rc;
        }
        pThis->fReadOnly = !(DrvStat.flags & FD_DISK_WRITABLE);
        rc = RTFileSeek(pThis->Os.hFileDevice, 0, RTFILE_SEEK_END, pcb);
    }
    else if (pThis->enmType == PDMMEDIATYPE_CDROM || pThis->enmType == PDMMEDIATYPE_DVD)
    {
        /* Clear the media-changed-since-last-call-thingy just to be on the safe side. */
        ioctl(RTFileToNative(pThis->Os.hFileDevice), CDROM_MEDIA_CHANGED, CDSL_CURRENT);
        rc = RTFileSeek(pThis->Os.hFileDevice, 0, RTFILE_SEEK_END, pcb);
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
    int rc = ioctl(RTFileToNative(pThis->Os.hFileDevice), CDROM_LOCKDOOR, (int)fLock);
    if (rc < 0)
    {
        if (errno == EBUSY)
            rc = VERR_ACCESS_DENIED;
        else if (errno == EDRIVE_CANT_DO_THIS)
            rc = VERR_NOT_SUPPORTED;
        else
            rc = RTErrConvertFromErrno(errno);
    }

    return rc;
}


DECLHIDDEN(int) drvHostBaseEjectOs(PDRVHOSTBASE pThis)
{
    int rc = ioctl(RTFileToNative(pThis->Os.hFileDevice), CDROMEJECT, 0);
    if (rc < 0)
    {
        if (errno == EBUSY)
            rc = VERR_PDM_MEDIA_LOCKED;
        else if (errno == ENOSYS)
            rc = VERR_NOT_SUPPORTED;
        else
            rc = RTErrConvertFromErrno(errno);
    }

    return rc;
}


DECLHIDDEN(int) drvHostBaseQueryMediaStatusOs(PDRVHOSTBASE pThis, bool *pfMediaChanged, bool *pfMediaPresent)
{
    int rc = VINF_SUCCESS;

    if (PDMMEDIATYPE_IS_FLOPPY(pThis->enmType))
    {
        floppy_drive_struct DrvStat;
        int rcLnx = ioctl(RTFileToNative(pThis->Os.hFileDevice), FDPOLLDRVSTAT, &DrvStat);
        if (!rcLnx)
        {
            bool fDiskIn = !(DrvStat.flags & (FD_VERIFY | FD_DISK_NEWCHANGE));
            *pfMediaPresent = fDiskIn;

            if (fDiskIn != pThis->Os.fPrevDiskIn)
                *pfMediaChanged = true;

            pThis->Os.fPrevDiskIn = fDiskIn;
        }
        else
            rc = RTErrConvertFromErrno(errno);
    }
    else
    {
        *pfMediaPresent = ioctl(RTFileToNative(pThis->Os.hFileDevice), CDROM_DRIVE_STATUS, CDSL_CURRENT) == CDS_DISC_OK;
        *pfMediaChanged = false;
        if (pThis->fMediaPresent != *pfMediaPresent)
            *pfMediaChanged = ioctl(RTFileToNative(pThis->Os.hFileDevice), CDROM_MEDIA_CHANGED, CDSL_CURRENT) == 1;
    }

    return rc;
}


DECLHIDDEN(void) drvHostBaseInitOs(PDRVHOSTBASE pThis)
{
    pThis->Os.hFileDevice    = NIL_RTFILE;
    pThis->Os.pbDoubleBuffer = NULL;
    pThis->Os.fPrevDiskIn    = false;
}


DECLHIDDEN(int) drvHostBaseOpenOs(PDRVHOSTBASE pThis, bool fReadOnly)
{
    uint32_t fFlags = (fReadOnly ? RTFILE_O_READ : RTFILE_O_READWRITE) | RTFILE_O_OPEN | RTFILE_O_DENY_NONE | RTFILE_O_NON_BLOCK;
    return RTFileOpen(&pThis->Os.hFileDevice, pThis->pszDevice, fFlags);
}


DECLHIDDEN(int) drvHostBaseMediaRefreshOs(PDRVHOSTBASE pThis)
{
    /*
     * Need to re-open the device because it will kill off any cached data
     * that Linux for some peculiar reason thinks should survive a media change.
     */
    if (pThis->Os.hFileDevice != NIL_RTFILE)
    {
        RTFileClose(pThis->Os.hFileDevice);
        pThis->Os.hFileDevice = NIL_RTFILE;
    }
    int rc = drvHostBaseOpenOs(pThis, pThis->fReadOnlyConfig);
    if (RT_FAILURE(rc))
    {
        if (!pThis->fReadOnlyConfig)
        {
            LogFlow(("%s-%d: drvHostBaseMediaRefreshOs: '%s' - retry readonly (%Rrc)\n",
                     pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, pThis->pszDevice, rc));
            rc = drvHostBaseOpenOs(pThis, true);
        }
        if (RT_FAILURE(rc))
        {
            LogFlow(("%s-%d: failed to open device '%s', rc=%Rrc\n",
                     pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance, pThis->pszDevice, rc));
            return rc;
        }
        pThis->fReadOnly = true;
    }
    else
        pThis->fReadOnly = pThis->fReadOnlyConfig;

    return rc;
}


DECLHIDDEN(bool) drvHostBaseIsMediaPollingRequiredOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);
    return true; /* On Linux we always use media polling. */
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

    if (pThis->Os.pbDoubleBuffer)
    {
        RTMemFree(pThis->Os.pbDoubleBuffer);
        pThis->Os.pbDoubleBuffer = NULL;
    }

    if (pThis->Os.hFileDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->Os.hFileDevice);
        AssertRC(rc);
        pThis->Os.hFileDevice = NIL_RTFILE;
    }
}

