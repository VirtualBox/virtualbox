/* $Id: DrvHostBase-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvHostBase - Host base drive access driver, Solaris specifics.
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
#include <fcntl.h>
#include <errno.h>
#include <stropts.h>
#include <malloc.h>
#include <sys/dkio.h>
#include <pwd.h>
#include <unistd.h>
#include <syslog.h>
#ifdef VBOX_WITH_SUID_WRAPPER
# include <auth_attr.h>
#endif
#include <sys/sockio.h>
#include <sys/scsi/scsi.h>

extern "C" char *getfullblkname(char *);

#include <VBox/err.h>
#include <iprt/file.h>
#include <iprt/string.h>

/**
 * Host backend specific data.
 */
typedef struct DRVHOSTBASEOS
{
    /** The filehandle of the device. */
    RTFILE                  hFileDevice;
    /** The raw filehandle of the device. */
    RTFILE                  hFileRawDevice;
    /** Device name of raw device (RTStrFree). */
    char                   *pszRawDeviceOpen;
} DRVHOSTBASEOS;
/** Pointer to the host backend specific data. */
typedef DRVHOSTBASEOS *PDRVHOSBASEOS;

//AssertCompile(sizeof(DRVHOSTBASEOS) <= 64);

#define DRVHOSTBASE_OS_INT_DECLARED
#include "DrvHostBase.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum buffer size we support, check whether darwin has some real upper limit. */
#define SOL_SCSI_MAX_BUFFER_SIZE (100 * _1K)


#ifdef VBOX_WITH_SUID_WRAPPER
/* These functions would have to go into a separate solaris binary with
 * the setuid permission set, which would run the user-SCSI ioctl and
 * return the value. BUT... this might be prohibitively slow.
 */

/**
 * Checks if the current user is authorized using Solaris' role-based access control.
 * Made as a separate function with so that it need not be invoked each time we need
 * to gain root access.
 *
 * @returns VBox error code.
 */
static int solarisCheckUserAuth()
{
    /* Uses Solaris' role-based access control (RBAC).*/
    struct passwd *pPass = getpwuid(getuid());
    if (pPass == NULL || chkauthattr("solaris.device.cdrw", pPass->pw_name) == 0)
        return VERR_PERMISSION_DENIED;

    return VINF_SUCCESS;
}

/**
 * Setuid wrapper to gain root access.
 *
 * @returns VBox error code.
 * @param   pEffUserID     Pointer to effective user ID.
 */
static int solarisEnterRootMode(uid_t *pEffUserID)
{
    /* Increase privilege if required */
    if (*pEffUserID != 0)
    {
        if (seteuid(0) == 0)
        {
            *pEffUserID = 0;
            return VINF_SUCCESS;
        }
        return VERR_PERMISSION_DENIED;
    }
    return VINF_SUCCESS;
}


/**
 * Setuid wrapper to relinquish root access.
 *
 * @returns VBox error code.
 * @param   pEffUserID     Pointer to effective user ID.
 */
static int solarisExitRootMode(uid_t *pEffUserID)
{
    /* Get back to user mode. */
    if (*pEffUserID == 0)
    {
        uid_t realID = getuid();
        if (seteuid(realID) == 0)
        {
            *pEffUserID = realID;
            return VINF_SUCCESS;
        }
        return VERR_PERMISSION_DENIED;
    }
    return VINF_SUCCESS;
}

#endif /* VBOX_WITH_SUID_WRAPPER */

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

    int rc = VERR_GENERAL_FAILURE;
    struct uscsi_cmd usc;
    union scsi_cdb scdb;
    memset(&usc, 0, sizeof(struct uscsi_cmd));
    memset(&scdb, 0, sizeof(scdb));

    switch (enmTxDir)
    {
        case PDMMEDIATXDIR_NONE:
            Assert(*pcbBuf == 0);
            usc.uscsi_flags = USCSI_READ;
            /* nothing to do */
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
            usc.uscsi_flags = USCSI_READ;
            break;
        case PDMMEDIATXDIR_TO_DEVICE:
            Assert(*pcbBuf != 0);
            usc.uscsi_flags = USCSI_WRITE;
            break;
        default:
            AssertMsgFailedReturn(("%d\n", enmTxDir), VERR_INTERNAL_ERROR);
    }
    usc.uscsi_flags |= USCSI_RQENABLE;
    usc.uscsi_rqbuf = (char *)pbSense;
    usc.uscsi_rqlen = cbSense;
    usc.uscsi_cdb = (caddr_t)&scdb;
    usc.uscsi_cdblen = 12;
    memcpy (usc.uscsi_cdb, pbCmd, usc.uscsi_cdblen);
    usc.uscsi_bufaddr = (caddr_t)pvBuf;
    usc.uscsi_buflen = *pcbBuf;
    usc.uscsi_timeout = (cTimeoutMillies + 999) / 1000;

    /* We need root privileges for user-SCSI under Solaris. */
#ifdef VBOX_WITH_SUID_WRAPPER
    uid_t effUserID = geteuid();
    solarisEnterRootMode(&effUserID); /** @todo check return code when this really works. */
#endif
    rc = ioctl(RTFileToNative(pThis->Os.hFileRawDevice), USCSICMD, &usc);
#ifdef VBOX_WITH_SUID_WRAPPER
    solarisExitRootMode(&effUserID);
#endif
    if (rc < 0)
    {
        if (errno == EPERM)
            return VERR_PERMISSION_DENIED;
        if (usc.uscsi_status)
        {
            rc = RTErrConvertFromErrno(errno);
            Log2(("%s: error status. rc=%Rrc\n", __FUNCTION__, rc));
        }
    }
    Log2(("%s: after ioctl: residual buflen=%d original buflen=%d\n", __FUNCTION__, usc.uscsi_resid, usc.uscsi_buflen));

    return rc;
}


DECLHIDDEN(size_t) drvHostBaseScsiCmdGetBufLimitOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);

    return SOL_SCSI_MAX_BUFFER_SIZE;
}


DECLHIDDEN(int) drvHostBaseGetMediaSizeOs(PDRVHOSTBASE pThis, uint64_t *pcb)
{
    /*
     * Sun docs suggests using DKIOCGGEOM instead of DKIOCGMEDIAINFO, but
     * Sun themselves use DKIOCGMEDIAINFO for DVDs/CDs, and use DKIOCGGEOM
     * for secondary storage devices.
     */
    struct dk_minfo MediaInfo;
    if (ioctl(RTFileToNative(pThis->Os.hFileRawDevice), DKIOCGMEDIAINFO, &MediaInfo) == 0)
    {
        *pcb = MediaInfo.dki_capacity * (uint64_t)MediaInfo.dki_lbsize;
        return VINF_SUCCESS;
    }
    return RTFileSeek(pThis->Os.hFileDevice, 0, RTFILE_SEEK_END, pcb);
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
    int rc = ioctl(RTFileToNative(pThis->Os.hFileRawDevice), fLock ? DKIOCLOCK : DKIOCUNLOCK, 0);
    if (rc < 0)
    {
        if (errno == EBUSY)
            rc = VERR_ACCESS_DENIED;
        else if (errno == ENOTSUP || errno == ENOSYS)
            rc = VERR_NOT_SUPPORTED;
        else
            rc = RTErrConvertFromErrno(errno);
    }

    return rc;
}


DECLHIDDEN(int) drvHostBaseEjectOs(PDRVHOSTBASE pThis)
{
    int rc = ioctl(RTFileToNative(pThis->Os.hFileRawDevice), DKIOCEJECT, 0);
    if (rc < 0)
    {
        if (errno == EBUSY)
            rc = VERR_PDM_MEDIA_LOCKED;
        else if (errno == ENOSYS || errno == ENOTSUP)
            rc = VERR_NOT_SUPPORTED;
        else if (errno == ENODEV)
            rc = VERR_PDM_MEDIA_NOT_MOUNTED;
        else
            rc = RTErrConvertFromErrno(errno);
    }

    return rc;
}


DECLHIDDEN(int) drvHostBaseQueryMediaStatusOs(PDRVHOSTBASE pThis, bool *pfMediaChanged, bool *pfMediaPresent)
{
    *pfMediaPresent = false;
    *pfMediaChanged = false;

    /* Need to pass the previous state and DKIO_NONE for the first time. */
    static dkio_state s_DeviceState = DKIO_NONE;
    dkio_state PreviousState = s_DeviceState;
    int rc = ioctl(RTFileToNative(pThis->Os.hFileRawDevice), DKIOCSTATE, &s_DeviceState);
    if (rc == 0)
    {
        *pfMediaPresent = (s_DeviceState == DKIO_INSERTED);
        if (PreviousState != s_DeviceState)
            *pfMediaChanged = true;
    }

    return VINF_SUCCESS;
}


DECLHIDDEN(void) drvHostBaseInitOs(PDRVHOSTBASE pThis)
{
    pThis->Os.hFileDevice      = NIL_RTFILE;
    pThis->Os.hFileRawDevice   = NIL_RTFILE;
    pThis->Os.pszRawDeviceOpen = NULL;
}


DECLHIDDEN(int) drvHostBaseOpenOs(PDRVHOSTBASE pThis, bool fReadOnly)
{
#ifdef VBOX_WITH_SUID_WRAPPER  /* Solaris setuid for Passthrough mode. */
    if (   (pThis->enmType == PDMMEDIATYPE_CDROM || pThis->enmType == PDMMEDIATYPE_DVD)
        && pThis->IMedia.pfnSendCmd)
    {
        rc = solarisCheckUserAuth();
        if (RT_FAILURE(rc))
        {
            Log(("DVD: solarisCheckUserAuth failed. Permission denied!\n"));
            return rc;
        }
    }
#endif /* VBOX_WITH_SUID_WRAPPER */

    char *pszBlockDevName = getfullblkname(pThis->pszDevice);
    if (!pszBlockDevName)
        return VERR_NO_MEMORY;
    pThis->pszDeviceOpen = RTStrDup(pszBlockDevName);  /* for RTStrFree() */
    free(pszBlockDevName);
    pThis->Os.pszRawDeviceOpen = RTStrDup(pThis->pszDevice);
    if (!pThis->pszDeviceOpen || !pThis->Os.pszRawDeviceOpen)
        return VERR_NO_MEMORY;

    unsigned fFlags = (fReadOnly ? RTFILE_O_READ : RTFILE_O_READWRITE)
                    | RTFILE_O_OPEN | RTFILE_O_DENY_NONE | RTFILE_O_NON_BLOCK;
    int rc = RTFileOpen(&pThis->Os.hFileDevice, pThis->pszDeviceOpen, fFlags);
    if (RT_SUCCESS(rc))
    {
        rc = RTFileOpen(&pThis->Os.hFileRawDevice, pThis->Os.pszRawDeviceOpen, fFlags);
        if (RT_SUCCESS(rc))
            return rc;

        LogRel(("DVD: failed to open device %s rc=%Rrc\n", pThis->Os.pszRawDeviceOpen, rc));
        RTFileClose(pThis->Os.hFileDevice);
    }
    else
        LogRel(("DVD: failed to open device %s rc=%Rrc\n", pThis->pszDeviceOpen, rc));
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

    AssertMsgFailed(("Solaris supports only CD/DVD host drive access\n"));
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

    if (pThis->Os.hFileRawDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->Os.hFileRawDevice);
        AssertRC(rc);
        pThis->Os.hFileRawDevice = NIL_RTFILE;
    }

    if (pThis->Os.pszRawDeviceOpen)
    {
        RTStrFree(pThis->Os.pszRawDeviceOpen);
        pThis->Os.pszRawDeviceOpen = NULL;
    }
}

