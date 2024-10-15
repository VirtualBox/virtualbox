/* $Id: DrvHostBase.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvHostBase - Host base drive access driver.
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

#ifndef VBOX_INCLUDED_SRC_Storage_DrvHostBase_h
#define VBOX_INCLUDED_SRC_Storage_DrvHostBase_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/critsect.h>
#include <iprt/log.h>
#include <iprt/semaphore.h>
#include <VBox/cdefs.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmstorageifs.h>

RT_C_DECLS_BEGIN


/** Pointer to host base drive access driver instance data. */
typedef struct DRVHOSTBASE *PDRVHOSTBASE;
/**
 * Host base drive access driver instance data.
 *
 * @implements PDMIMOUNT
 * @implements PDMIMEDIA
 */
typedef struct DRVHOSTBASE
{
    /** Critical section used to serialize access to the handle and other
     * members of this struct. */
    RTCRITSECT              CritSect;
    /** Pointer driver instance. */
    PPDMDRVINS              pDrvIns;
    /** Drive type. */
    PDMMEDIATYPE            enmType;
    /** Visible to the BIOS. */
    bool                    fBiosVisible;
    /** The configuration readonly value. */
    bool                    fReadOnlyConfig;
    /** The current readonly status. */
    bool                    fReadOnly;
    /** Flag whether failure to attach is an error or not. */
    bool                    fAttachFailError;
    /** Flag whether to keep instance working (as unmounted though). */
    bool                    fKeepInstance;
    /** Device name (MMHeap). */
    char                   *pszDevice;
    /** Device name to open (RTStrFree). */
    char                   *pszDeviceOpen;
    /** Uuid of the drive. */
    RTUUID                  Uuid;

    /** Pointer to the media port interface above us. */
    PPDMIMEDIAPORT          pDrvMediaPort;
    /** Pointer to the extended media port interface above us. */
    PPDMIMEDIAEXPORT        pDrvMediaExPort;
    /** Pointer to the mount notify interface above us. */
    PPDMIMOUNTNOTIFY        pDrvMountNotify;
    /** Our media interface. */
    PDMIMEDIA               IMedia;
    /** Our extended media interface. */
    PDMIMEDIAEX             IMediaEx;
    /** Our mountable interface. */
    PDMIMOUNT               IMount;

    /** Media present indicator. */
    bool volatile           fMediaPresent;
    /** Locked indicator. */
    bool                    fLocked;
    /** The size of the media currently in the drive.
     * This is invalid if no drive is in the drive. */
    uint64_t volatile       cbSize;

    /** Handle of the poller thread. */
    RTTHREAD                ThreadPoller;
    /** Event semaphore the thread will wait on. */
    RTSEMEVENT              EventPoller;
    /** The poller interval. */
    RTMSINTERVAL            cMilliesPoller;
    /** The shutdown indicator. */
    bool volatile           fShutdownPoller;

    /** BIOS PCHS geometry. */
    PDMMEDIAGEOMETRY        PCHSGeometry;
    /** BIOS LCHS geometry. */
    PDMMEDIAGEOMETRY        LCHSGeometry;

    /** Pointer to the current buffer holding data. */
    void                    *pvBuf;
    /** Size of the buffer. */
    size_t                  cbBuf;
    /** Size of the I/O request to allocate. */
    size_t                  cbIoReqAlloc;

    /** Release statistics: number of bytes written. */
    STAMCOUNTER              StatBytesWritten;
    /** Release statistics: number of bytes read. */
    STAMCOUNTER              StatBytesRead;
    /** Release statistics: Number of requests submitted. */
    STAMCOUNTER              StatReqsSubmitted;
    /** Release statistics: Number of requests failed. */
    STAMCOUNTER              StatReqsFailed;
    /** Release statistics: Number of requests succeeded. */
    STAMCOUNTER              StatReqsSucceeded;
    /** Release statistics: Number of flush requests. */
    STAMCOUNTER              StatReqsFlush;
    /** Release statistics: Number of write requests. */
    STAMCOUNTER              StatReqsWrite;
    /** Release statistics: Number of read requests. */
    STAMCOUNTER              StatReqsRead;

    /**
     * Performs the locking / unlocking of the device.
     *
     * This callback pointer should be set to NULL if the device doesn't support this action.
     *
     * @returns VBox status code.
     * @param   pThis       Pointer to the instance data.
     * @param   fLock       Set if locking, clear if unlocking.
     */
    DECLCALLBACKMEMBER(int, pfnDoLock,(PDRVHOSTBASE pThis, bool fLock));

    union
    {
#ifdef DRVHOSTBASE_OS_INT_DECLARED
        DRVHOSTBASEOS       Os;
#endif
        uint8_t             abPadding[64];
    };
} DRVHOSTBASE;


/**
 * Request structure fo a request.
 */
typedef struct DRVHOSTBASEREQ
{
    /** Transfer size. */
    size_t                   cbReq;
    /** Amount of residual data. */
    size_t                   cbResidual;
    /** Start of the request data for the device above us. */
    uint8_t                  abAlloc[1];
} DRVHOSTBASEREQ;
/** Pointer to a request structure. */
typedef DRVHOSTBASEREQ *PDRVHOSTBASEREQ;

DECLHIDDEN(int) DRVHostBaseInit(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, const char *pszCfgValid, PDMMEDIATYPE enmType);
DECLHIDDEN(int) DRVHostBaseMediaPresent(PDRVHOSTBASE pThis);
DECLHIDDEN(void) DRVHostBaseMediaNotPresent(PDRVHOSTBASE pThis);
DECLCALLBACK(void) DRVHostBaseDestruct(PPDMDRVINS pDrvIns);

DECLHIDDEN(int) drvHostBaseScsiCmdOs(PDRVHOSTBASE pThis, const uint8_t *pbCmd, size_t cbCmd, PDMMEDIATXDIR enmTxDir,
                                     void *pvBuf, uint32_t *pcbBuf, uint8_t *pbSense, size_t cbSense, uint32_t cTimeoutMillies);
DECLHIDDEN(size_t) drvHostBaseScsiCmdGetBufLimitOs(PDRVHOSTBASE pThis);
DECLHIDDEN(int) drvHostBaseGetMediaSizeOs(PDRVHOSTBASE pThis, uint64_t *pcb);
DECLHIDDEN(int) drvHostBaseReadOs(PDRVHOSTBASE pThis, uint64_t off, void *pvBuf, size_t cbRead);
DECLHIDDEN(int) drvHostBaseWriteOs(PDRVHOSTBASE pThis, uint64_t off, const void *pvBuf, size_t cbWrite);
DECLHIDDEN(int) drvHostBaseFlushOs(PDRVHOSTBASE pThis);
DECLHIDDEN(int) drvHostBaseDoLockOs(PDRVHOSTBASE pThis, bool fLock);
DECLHIDDEN(int) drvHostBaseEjectOs(PDRVHOSTBASE pThis);

DECLHIDDEN(void) drvHostBaseInitOs(PDRVHOSTBASE pThis);
DECLHIDDEN(int) drvHostBaseOpenOs(PDRVHOSTBASE pThis, bool fReadOnly);
DECLHIDDEN(int) drvHostBaseMediaRefreshOs(PDRVHOSTBASE pThis);
DECLHIDDEN(int) drvHostBaseQueryMediaStatusOs(PDRVHOSTBASE pThis, bool *pfMediaChanged, bool *pfMediaPresent);
DECLHIDDEN(bool) drvHostBaseIsMediaPollingRequiredOs(PDRVHOSTBASE pThis);
DECLHIDDEN(void) drvHostBaseDestructOs(PDRVHOSTBASE pThis);

DECLHIDDEN(int) drvHostBaseBufferRetain(PDRVHOSTBASE pThis, PDRVHOSTBASEREQ pReq, size_t cbBuf, bool fWrite, void **ppvBuf);
DECLHIDDEN(int) drvHostBaseBufferRelease(PDRVHOSTBASE pThis, PDRVHOSTBASEREQ pReq, size_t cbBuf, bool fWrite, void *pvBuf);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Storage_DrvHostBase_h */
