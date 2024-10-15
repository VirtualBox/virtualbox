/* $Id: DrvVD.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvVD - Generic VBox disk media driver.
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
*   Header files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_VD
#include <VBox/vd.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/vmm/pdmasynccompletion.h>
#include <VBox/vmm/pdmblkcache.h>
#include <VBox/vmm/ssm.h>
#include <iprt/asm.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/sg.h>
#include <iprt/system.h>
#include <iprt/memsafer.h>
#include <iprt/memcache.h>
#include <iprt/list.h>

#ifdef VBOX_WITH_INIP
/* All lwip header files are not C++ safe. So hack around this. */
RT_C_DECLS_BEGIN
#include <lwip/opt.h>
#include <lwip/inet.h>
#include <lwip/tcp.h>
#include <lwip/sockets.h>
# if LWIP_IPV6
#  include <lwip/inet6.h>
# endif
RT_C_DECLS_END
#endif /* VBOX_WITH_INIP */

#include "HBDMgmt.h"
#include "IOBufMgmt.h"

#include "VBoxDD.h"

#ifdef VBOX_WITH_INIP
/* Small hack to get at lwIP initialized status */
extern bool DevINIPConfigured(void);
#endif /* VBOX_WITH_INIP */


/** @def VBOX_PERIODIC_FLUSH
 * Enable support for periodically flushing the VDI to disk. This may prove
 * useful for those nasty problems with the ultra-slow host filesystems.
 * If this is enabled, it can be configured via the CFGM key
 * "VBoxInternal/Devices/piix3ide/0/LUN#<x>/Config/FlushInterval". @verbatim<x>@endverbatim
 * must be replaced with the correct LUN number of the disk that should
 * do the periodic flushes. The value of the key is the number of bytes
 * written between flushes. A value of 0 (the default) denotes no flushes. */
#define VBOX_PERIODIC_FLUSH

/** @def VBOX_IGNORE_FLUSH
 * Enable support for ignoring VDI flush requests. This can be useful for
 * filesystems that show bad guest IDE write performance (especially with
 * Windows guests). NOTE that this does not disable the flushes caused by
 * the periodic flush cache feature above.
 * If this feature is enabled, it can be configured via the CFGM key
 * "VBoxInternal/Devices/piix3ide/0/LUN#<x>/Config/IgnoreFlush". @verbatim<x>@endverbatim
 * must be replaced with the correct LUN number of the disk that should
 * ignore flush requests. The value of the key is a boolean. The default
 * is to ignore flushes, i.e. true. */
#define VBOX_IGNORE_FLUSH


/*********************************************************************************************************************************
*   Defined types, constants and macros                                                                                          *
*********************************************************************************************************************************/

/** Converts a pointer to VBOXDISK::IMedia to a PVBOXDISK. */
#define PDMIMEDIA_2_VBOXDISK(pInterface) \
    ( (PVBOXDISK)((uintptr_t)pInterface - RT_UOFFSETOF(VBOXDISK, IMedia)) )

/** Saved state version of an I/O request .*/
#define DRVVD_IOREQ_SAVED_STATE_VERSION UINT32_C(1)
/** Maximum number of request errors in the release log before muting. */
#define DRVVD_MAX_LOG_REL_ERRORS        100

/** Forward declaration for the dis kcontainer. */
typedef struct VBOXDISK *PVBOXDISK;

/**
 * VBox disk container, image information, private part.
 */

typedef struct VBOXIMAGE
{
    /** Pointer to next image. */
    struct VBOXIMAGE   *pNext;
    /** Pointer to list of VD interfaces. Per-image. */
    PVDINTERFACE       pVDIfsImage;
    /** Configuration information interface. */
    VDINTERFACECONFIG  VDIfConfig;
    /** TCP network stack instance for host mode. */
    VDIFINST           hVdIfTcpNet;
    /** TCP network stack interface (for INIP). */
    VDINTERFACETCPNET  VDIfTcpNet;
    /** I/O interface. */
    VDINTERFACEIO      VDIfIo;
} VBOXIMAGE, *PVBOXIMAGE;

/**
 * Storage backend data.
 */
typedef struct DRVVDSTORAGEBACKEND
{
    /** The virtual disk driver instance. */
    PVBOXDISK                   pVD;
    /** PDM async completion end point. */
    PPDMASYNCCOMPLETIONENDPOINT pEndpoint;
    /** The template. */
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
    /** Event semaphore for synchronous operations. */
    RTSEMEVENT                  EventSem;
    /** Flag whether a synchronous operation is currently pending. */
    volatile bool               fSyncIoPending;
    /** Return code of the last completed request. */
    int                         rcReqLast;
    /** Callback routine */
    PFNVDCOMPLETED              pfnCompleted;
} DRVVDSTORAGEBACKEND, *PDRVVDSTORAGEBACKEND;

/**
 * VD I/O request state.
 */
typedef enum VDIOREQSTATE
{
    /** Invalid. */
    VDIOREQSTATE_INVALID = 0,
    /** The request is not in use and resides on the free list. */
    VDIOREQSTATE_FREE,
    /** The request was just allocated and is not active. */
    VDIOREQSTATE_ALLOCATED,
    /** The request was allocated and is in use. */
    VDIOREQSTATE_ACTIVE,
    /** The request was suspended and is not actively processed. */
    VDIOREQSTATE_SUSPENDED,
    /** The request is in the last step of completion and syncs memory. */
    VDIOREQSTATE_COMPLETING,
    /** The request completed. */
    VDIOREQSTATE_COMPLETED,
    /** The request was aborted but wasn't returned as complete from the storage
     * layer below us. */
    VDIOREQSTATE_CANCELED,
    /** 32bit hack. */
    VDIOREQSTATE_32BIT_HACK = 0x7fffffff
} VDIOREQSTATE;

/**
 * VD I/O Request.
 */
typedef struct PDMMEDIAEXIOREQINT
{
    /** List node for the list of allocated requests. */
    RTLISTNODE                    NdAllocatedList;
    /** List for requests waiting for I/O memory or on the redo list. */
    RTLISTNODE                    NdLstWait;
    /** I/O request type. */
    PDMMEDIAEXIOREQTYPE           enmType;
    /** Request state. */
    volatile VDIOREQSTATE         enmState;
    /** I/O request ID. */
    PDMMEDIAEXIOREQID             uIoReqId;
    /** Pointer to the disk container. */
    PVBOXDISK                     pDisk;
    /** Flags. */
    uint32_t                      fFlags;
    /** Timestamp when the request was submitted. */
    uint64_t                      tsSubmit;
    /** Type dependent data. */
    union
    {
        /** Read/Write request sepcific data. */
        struct
        {
            /** Start offset of the request. */
            uint64_t                      offStart;
            /** Size of the request. */
            size_t                        cbReq;
            /** Size left for this request. */
            size_t                        cbReqLeft;
            /** Size of the allocated I/O buffer. */
            size_t                        cbIoBuf;
            /** Pointer to the S/G buffer. */
            PRTSGBUF                      pSgBuf;
            /** Flag whether the pointer is a direct buffer or
             *  was allocated by us. */
            bool                          fDirectBuf;
            /** Buffer management data based on the fDirectBuf flag. */
            union
            {
                /** Direct buffer. */
                struct
                {
                    /** Segment for the data buffer. */
                    RTSGSEG               Seg;
                    /** S/G buffer structure. */
                    RTSGBUF               SgBuf;
                } Direct;
                /** I/O buffer descriptor. */
                IOBUFDESC                 IoBuf;
            };
        } ReadWrite;
        /** Discard specific data. */
        struct
        {
            /** Pointer to array of ranges to discard. */
            PRTRANGE                      paRanges;
            /** Number of ranges to discard. */
            unsigned                      cRanges;
        } Discard;
    };
    /** Allocator specific memory - variable size. */
    uint8_t                       abAlloc[1];
} PDMMEDIAEXIOREQINT;
/** Pointer to a VD I/O request. */
typedef PDMMEDIAEXIOREQINT *PPDMMEDIAEXIOREQINT;

/**
 * Structure for holding a list of allocated requests.
 */
typedef struct VDLSTIOREQALLOC
{
    /** Mutex protecting the table of allocated requests. */
    RTSEMFASTMUTEX           hMtxLstIoReqAlloc;
    /** List anchor. */
    RTLISTANCHOR             LstIoReqAlloc;
} VDLSTIOREQALLOC;
typedef VDLSTIOREQALLOC *PVDLSTIOREQALLOC;

/** Number of bins for allocated requests. */
#define DRVVD_VDIOREQ_ALLOC_BINS    8

/**
 * VD config node.
 */
typedef struct VDCFGNODE
{
    /** List node for the list of config nodes. */
    RTLISTNODE              NdLst;
    /** Pointer to the driver helper callbacks. */
    PCPDMDRVHLPR3           pHlp;
    /** The config node. */
    PCFGMNODE               pCfgNode;
} VDCFGNODE;
/** Pointer to a VD config node. */
typedef VDCFGNODE *PVDCFGNODE;

/**
 * VBox disk container media main structure, private part.
 *
 * @implements  PDMIMEDIA
 * @implements  PDMIMEDIAEX
 * @implements  PDMIMOUNT
 * @implements  VDINTERFACEERROR
 * @implements  VDINTERFACETCPNET
 * @implements  VDINTERFACEASYNCIO
 * @implements  VDINTERFACECONFIG
 */
typedef struct VBOXDISK
{
    /** The VBox disk container. */
    PVDISK                   pDisk;
    /** The media interface. */
    PDMIMEDIA                IMedia;
    /** Media port. */
    PPDMIMEDIAPORT           pDrvMediaPort;
    /** Pointer to the driver instance. */
    PPDMDRVINS               pDrvIns;
    /** Flag whether suspend has changed image open mode to read only. */
    bool                     fTempReadOnly;
    /** Flag whether to use the runtime (true) or startup error facility. */
    bool                     fErrorUseRuntime;
    /** Pointer to list of VD interfaces. Per-disk. */
    PVDINTERFACE             pVDIfsDisk;
    /** Error interface. */
    VDINTERFACEERROR         VDIfError;
    /** Thread synchronization interface. */
    VDINTERFACETHREADSYNC    VDIfThreadSync;

    /** Flag whether opened disk supports async I/O operations. */
    bool                     fAsyncIOSupported;
    /** Pointer to the list of data we need to keep per image. */
    PVBOXIMAGE               pImages;
    /** Flag whether the media should allow concurrent open for writing. */
    bool                     fShareable;
    /** Flag whether a merge operation has been set up. */
    bool                     fMergePending;
    /** Synchronization to prevent destruction before merge finishes. */
    RTSEMFASTMUTEX           MergeCompleteMutex;
    /** Synchronization between merge and other image accesses. */
    RTSEMRW                  MergeLock;
    /** Source image index for merging. */
    unsigned                 uMergeSource;
    /** Target image index for merging. */
    unsigned                 uMergeTarget;

    /** Flag whether boot acceleration is enabled. */
    bool                     fBootAccelEnabled;
    /** Flag whether boot acceleration is currently active. */
    bool                     fBootAccelActive;
    /** Size of the disk, used for read truncation. */
    uint64_t                 cbDisk;
    /** Size of the configured buffer. */
    size_t                   cbBootAccelBuffer;
    /** Start offset for which the buffer holds data. */
    uint64_t                 offDisk;
    /** Number of valid bytes in the buffer. */
    size_t                   cbDataValid;
    /** The disk buffer. */
    uint8_t                 *pbData;
    /** Bandwidth group the disk is assigned to. */
    char                    *pszBwGroup;
    /** Flag whether async I/O using the host cache is enabled. */
    bool                     fAsyncIoWithHostCache;

    /** I/O interface for a cache image. */
    VDINTERFACEIO            VDIfIoCache;
    /** Interface list for the cache image. */
    PVDINTERFACE             pVDIfsCache;

    /** The block cache handle if configured. */
    PPDMBLKCACHE             pBlkCache;
    /** Host block device manager. */
    HBDMGR                   hHbdMgr;

    /** Drive type. */
    PDMMEDIATYPE            enmType;
    /** Locked indicator. */
    bool                    fLocked;
    /** Mountable indicator. */
    bool                    fMountable;
    /** Visible to the BIOS. */
    bool                    fBiosVisible;
    /** Flag whether this medium should be presented as non rotational. */
    bool                    fNonRotational;
    /** Flag whether a suspend is in progress right now. */
    volatile bool           fSuspending;
#ifdef VBOX_PERIODIC_FLUSH
    /** HACK: Configuration value for number of bytes written after which to flush. */
    uint32_t                cbFlushInterval;
    /** HACK: Current count for the number of bytes written since the last flush. */
    uint32_t                cbDataWritten;
#endif /* VBOX_PERIODIC_FLUSH */
#ifdef VBOX_IGNORE_FLUSH
    /** HACK: Disable flushes for this drive. */
    bool                    fIgnoreFlush;
    /** Disable async flushes for this drive. */
    bool                    fIgnoreFlushAsync;
#endif /* VBOX_IGNORE_FLUSH */
    /** Our mountable interface. */
    PDMIMOUNT               IMount;
    /** Pointer to the mount notify interface above us. */
    PPDMIMOUNTNOTIFY        pDrvMountNotify;
    /** Uuid of the drive. */
    RTUUID                  Uuid;
    /** BIOS PCHS Geometry. */
    PDMMEDIAGEOMETRY        PCHSGeometry;
    /** BIOS LCHS Geometry. */
    PDMMEDIAGEOMETRY        LCHSGeometry;
    /** Region list. */
    PVDREGIONLIST           pRegionList;

    /** VD config support.
     * @{ */
    /** List head of config nodes. */
    RTLISTANCHOR            LstCfgNodes;
    /** @} */

    /** Cryptographic support
     * @{ */
    /** Pointer to the CFGM node containing the config of the crypto filter
     * if enable. */
    VDCFGNODE                CfgCrypto;
    /** Config interface for the encryption filter. */
    VDINTERFACECONFIG        VDIfCfg;
    /** Crypto interface for the encryption filter. */
    VDINTERFACECRYPTO        VDIfCrypto;
    /** The secret key interface used to retrieve keys. */
    PPDMISECKEY              pIfSecKey;
    /** The secret key helper interface used to notify about missing keys. */
    PPDMISECKEYHLP           pIfSecKeyHlp;
    /** @} */

    /** @name IMEDIAEX interface support specific members.
     * @{ */
    /** Pointer to the IMEDIAEXPORT interface above us. */
    PPDMIMEDIAEXPORT         pDrvMediaExPort;
    /** Our extended media interface. */
    PDMIMEDIAEX              IMediaEx;
    /** Memory cache for the I/O requests. */
    RTMEMCACHE               hIoReqCache;
    /** I/O buffer manager. */
    IOBUFMGR                 hIoBufMgr;
    /** Active request counter. */
    volatile uint32_t        cIoReqsActive;
    /** Bins for allocated requests. */
    VDLSTIOREQALLOC          aIoReqAllocBins[DRVVD_VDIOREQ_ALLOC_BINS];
    /** List of requests for I/O memory to be available - VDIOREQ::NdLstWait. */
    RTLISTANCHOR             LstIoReqIoBufWait;
    /** Critical section protecting the list of requests waiting for I/O memory. */
    RTCRITSECT               CritSectIoReqsIoBufWait;
    /** Number of requests waiting for a I/O buffer. */
    volatile uint32_t        cIoReqsWaiting;
    /** Flag whether we have to resubmit requests on resume because the
     * VM was suspended due to a recoverable I/O error.
     */
    volatile bool            fRedo;
    /** List of requests we have to redo. */
    RTLISTANCHOR             LstIoReqRedo;
    /** Criticial section protecting the list of waiting requests. */
    RTCRITSECT               CritSectIoReqRedo;
    /** Number of errors logged so far. */
    unsigned                 cErrors;
    /** @} */

    /** @name Statistics.
     * @{ */
    /** How many attempts were made to query a direct buffer pointer from the
     * device/driver above. */
    STAMCOUNTER              StatQueryBufAttempts;
    /** How many attempts to query a direct buffer pointer succeeded. */
    STAMCOUNTER              StatQueryBufSuccess;
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
    /** Release statistics: Number of discard requests. */
    STAMCOUNTER              StatReqsDiscard;
    /** Release statistics: Number of I/O requests processed per second. */
    STAMCOUNTER              StatReqsPerSec;
    /** @} */
} VBOXDISK;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

static DECLCALLBACK(void) drvvdMediaExIoReqComplete(void *pvUser1, void *pvUser2, int rcReq);
static void drvvdPowerOffOrDestructOrUnmount(PPDMDRVINS pDrvIns);
DECLINLINE(void) drvvdMediaExIoReqBufFree(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq);
static int drvvdMediaExIoReqCompleteWorker(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, int rcReq, bool fUpNotify);
static int drvvdMediaExIoReqReadWriteProcess(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, bool fUpNotify);

/**
 * Internal: allocate new image descriptor and put it in the list
 */
static PVBOXIMAGE drvvdNewImage(PVBOXDISK pThis)
{
    AssertPtr(pThis);
    PVBOXIMAGE pImage = (PVBOXIMAGE)RTMemAllocZ(sizeof(VBOXIMAGE));
    if (pImage)
    {
        pImage->pVDIfsImage = NULL;
        PVBOXIMAGE *pp = &pThis->pImages;
        while (*pp != NULL)
            pp = &(*pp)->pNext;
        *pp = pImage;
        pImage->pNext = NULL;
    }

    return pImage;
}

/**
 * Internal: free the list of images descriptors.
 */
static void drvvdFreeImages(PVBOXDISK pThis)
{
    while (pThis->pImages != NULL)
    {
        PVBOXIMAGE p = pThis->pImages;
        pThis->pImages = pThis->pImages->pNext;
        if (p->hVdIfTcpNet != NULL)
            VDIfTcpNetInstDefaultDestroy(p->hVdIfTcpNet);
        RTMemFree(p);
    }
}


/**
 * Make the image temporarily read-only.
 *
 * @returns VBox status code.
 * @param   pThis               The driver instance data.
 */
static int drvvdSetReadonly(PVBOXDISK pThis)
{
    int rc = VINF_SUCCESS;
    if (   pThis->pDisk
        && !VDIsReadOnly(pThis->pDisk))
    {
        unsigned uOpenFlags;
        rc = VDGetOpenFlags(pThis->pDisk, VD_LAST_IMAGE, &uOpenFlags);
        AssertRC(rc);
        uOpenFlags |= VD_OPEN_FLAGS_READONLY;
        rc = VDSetOpenFlags(pThis->pDisk, VD_LAST_IMAGE, uOpenFlags);
        AssertRC(rc);
        pThis->fTempReadOnly = true;
    }
    return rc;
}


/**
 * Undo the temporary read-only status of the image.
 *
 * @returns VBox status code.
 * @param   pThis               The driver instance data.
 */
static int drvvdSetWritable(PVBOXDISK pThis)
{
    int rc = VINF_SUCCESS;
    if (pThis->fTempReadOnly)
    {
        unsigned uOpenFlags;
        rc = VDGetOpenFlags(pThis->pDisk, VD_LAST_IMAGE, &uOpenFlags);
        AssertRC(rc);
        uOpenFlags &= ~VD_OPEN_FLAGS_READONLY;
        rc = VDSetOpenFlags(pThis->pDisk, VD_LAST_IMAGE, uOpenFlags);
        if (RT_SUCCESS(rc))
            pThis->fTempReadOnly = false;
        else
            AssertRC(rc);
    }
    return rc;
}


/*********************************************************************************************************************************
*   Error reporting callback                                                                                                     *
*********************************************************************************************************************************/

static DECLCALLBACK(void) drvvdErrorCallback(void *pvUser, int rc, RT_SRC_POS_DECL,
                                             const char *pszFormat, va_list va)
{
    PPDMDRVINS pDrvIns = (PPDMDRVINS)pvUser;
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    if (pThis->fErrorUseRuntime)
        /* We must not pass VMSETRTERR_FLAGS_FATAL as it could lead to a
         * deadlock: We are probably executed in a thread context != EMT
         * and the EM thread would wait until every thread is suspended
         * but we would wait for the EM thread ... */

        PDMDrvHlpVMSetRuntimeErrorV(pDrvIns, /* fFlags=*/ 0, "DrvVD", pszFormat, va);
    else
        PDMDrvHlpVMSetErrorV(pDrvIns, rc, RT_SRC_POS_ARGS, pszFormat, va);
}


/*********************************************************************************************************************************
*   VD Async I/O interface implementation                                                                                        *
*********************************************************************************************************************************/

#ifdef VBOX_WITH_PDM_ASYNC_COMPLETION

static DECLCALLBACK(void) drvvdAsyncTaskCompleted(PPDMDRVINS pDrvIns, void *pvTemplateUser, void *pvUser, int rcReq)
{
    RT_NOREF(pDrvIns);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pvTemplateUser;

    LogFlowFunc(("pDrvIns=%#p pvTemplateUser=%#p pvUser=%#p rcReq=%d\n",
                 pDrvIns, pvTemplateUser, pvUser, rcReq));

    if (pStorageBackend->fSyncIoPending)
    {
        Assert(!pvUser);
        pStorageBackend->rcReqLast      = rcReq;
        ASMAtomicWriteBool(&pStorageBackend->fSyncIoPending, false);
        RTSemEventSignal(pStorageBackend->EventSem);
    }
    else
    {
        int rc;

        AssertPtr(pvUser);

        AssertPtr(pStorageBackend->pfnCompleted);
        rc = pStorageBackend->pfnCompleted(pvUser, rcReq);
        AssertRC(rc);
    }
}

static DECLCALLBACK(int) drvvdAsyncIOOpen(void *pvUser, const char *pszLocation,
                                          uint32_t fOpen,
                                          PFNVDCOMPLETED pfnCompleted,
                                          void **ppStorage)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;
    PDRVVDSTORAGEBACKEND pStorageBackend = NULL;
    int rc = VINF_SUCCESS;

    /*
     * Check whether the backend wants to open a block device and try to prepare it
     * if we didn't claim it yet.
     *
     * We only create a block device manager on demand to not waste any resources.
     */
    if (HBDMgrIsBlockDevice(pszLocation))
    {
        if (pThis->hHbdMgr == NIL_HBDMGR)
            rc = HBDMgrCreate(&pThis->hHbdMgr);

        if (   RT_SUCCESS(rc)
            && !HBDMgrIsBlockDeviceClaimed(pThis->hHbdMgr, pszLocation))
            rc = HBDMgrClaimBlockDevice(pThis->hHbdMgr, pszLocation);

        if (RT_FAILURE(rc))
            return rc;
    }

    pStorageBackend = (PDRVVDSTORAGEBACKEND)RTMemAllocZ(sizeof(DRVVDSTORAGEBACKEND));
    if (pStorageBackend)
    {
        pStorageBackend->pVD            = pThis;
        pStorageBackend->fSyncIoPending = false;
        pStorageBackend->rcReqLast      = VINF_SUCCESS;
        pStorageBackend->pfnCompleted   = pfnCompleted;

        rc = RTSemEventCreate(&pStorageBackend->EventSem);
        if (RT_SUCCESS(rc))
        {
            rc = PDMDrvHlpAsyncCompletionTemplateCreate(pThis->pDrvIns, &pStorageBackend->pTemplate,
                                                        drvvdAsyncTaskCompleted, pStorageBackend, "AsyncTaskCompleted");
            if (RT_SUCCESS(rc))
            {
                uint32_t fFlags = (fOpen & RTFILE_O_ACCESS_MASK) == RTFILE_O_READ
                                ? PDMACEP_FILE_FLAGS_READ_ONLY
                                : 0;
                if (pThis->fShareable)
                {
                    Assert((fOpen & RTFILE_O_DENY_MASK) == RTFILE_O_DENY_NONE);

                    fFlags |= PDMACEP_FILE_FLAGS_DONT_LOCK;
                }
                if (pThis->fAsyncIoWithHostCache)
                    fFlags |= PDMACEP_FILE_FLAGS_HOST_CACHE_ENABLED;

                rc = PDMDrvHlpAsyncCompletionEpCreateForFile(pThis->pDrvIns,
                                                             &pStorageBackend->pEndpoint,
                                                             pszLocation, fFlags,
                                                             pStorageBackend->pTemplate);

                if (RT_SUCCESS(rc))
                {
                    if (pThis->pszBwGroup)
                        rc = PDMDrvHlpAsyncCompletionEpSetBwMgr(pThis->pDrvIns, pStorageBackend->pEndpoint, pThis->pszBwGroup);

                    if (RT_SUCCESS(rc))
                    {
                        LogFlow(("drvvdAsyncIOOpen: Successfully opened '%s'; fOpen=%#x pStorage=%p\n",
                                 pszLocation, fOpen, pStorageBackend));
                        *ppStorage = pStorageBackend;
                        return VINF_SUCCESS;
                    }

                    PDMDrvHlpAsyncCompletionEpClose(pThis->pDrvIns, pStorageBackend->pEndpoint);
                }

                PDMDrvHlpAsyncCompletionTemplateDestroy(pThis->pDrvIns, pStorageBackend->pTemplate);
            }
            RTSemEventDestroy(pStorageBackend->EventSem);
        }
        RTMemFree(pStorageBackend);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

static DECLCALLBACK(int) drvvdAsyncIOClose(void *pvUser, void *pStorage)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;

    /*
     * We don't unclaim any block devices on purpose here because they
     * might get reopened shortly (switching to readonly during suspend)
     *
     * Block devices will get unclaimed during destruction of the driver.
     */

    PDMDrvHlpAsyncCompletionEpClose(pThis->pDrvIns, pStorageBackend->pEndpoint);
    PDMDrvHlpAsyncCompletionTemplateDestroy(pThis->pDrvIns, pStorageBackend->pTemplate);
    RTSemEventDestroy(pStorageBackend->EventSem);
    RTMemFree(pStorageBackend);
    return VINF_SUCCESS;;
}

static DECLCALLBACK(int) drvvdAsyncIOReadSync(void *pvUser, void *pStorage, uint64_t uOffset,
                                              void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;
    RTSGSEG DataSeg;
    PPDMASYNCCOMPLETIONTASK pTask;

    bool fOld = ASMAtomicXchgBool(&pStorageBackend->fSyncIoPending, true);
    Assert(!fOld); NOREF(fOld);
    DataSeg.cbSeg = cbRead;
    DataSeg.pvSeg = pvBuf;

    int rc = PDMDrvHlpAsyncCompletionEpRead(pThis->pDrvIns, pStorageBackend->pEndpoint, uOffset, &DataSeg, 1, cbRead, NULL, &pTask);
    if (RT_FAILURE(rc))
        return rc;

    if (rc == VINF_AIO_TASK_PENDING)
    {
        /* Wait */
        rc = RTSemEventWait(pStorageBackend->EventSem, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }
    else
        ASMAtomicXchgBool(&pStorageBackend->fSyncIoPending, false);

    if (pcbRead)
        *pcbRead = cbRead;

    return pStorageBackend->rcReqLast;
}

static DECLCALLBACK(int) drvvdAsyncIOWriteSync(void *pvUser, void *pStorage, uint64_t uOffset,
                                               const void *pvBuf, size_t cbWrite, size_t *pcbWritten)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;
    RTSGSEG DataSeg;
    PPDMASYNCCOMPLETIONTASK pTask;

    bool fOld = ASMAtomicXchgBool(&pStorageBackend->fSyncIoPending, true);
    Assert(!fOld); NOREF(fOld);
    DataSeg.cbSeg = cbWrite;
    DataSeg.pvSeg = (void *)pvBuf;

    int rc = PDMDrvHlpAsyncCompletionEpWrite(pThis->pDrvIns, pStorageBackend->pEndpoint, uOffset, &DataSeg, 1, cbWrite, NULL, &pTask);
    if (RT_FAILURE(rc))
        return rc;

    if (rc == VINF_AIO_TASK_PENDING)
    {
        /* Wait */
        rc = RTSemEventWait(pStorageBackend->EventSem, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }
    else
        ASMAtomicXchgBool(&pStorageBackend->fSyncIoPending, false);

    if (pcbWritten)
        *pcbWritten = cbWrite;

    return pStorageBackend->rcReqLast;
}

static DECLCALLBACK(int) drvvdAsyncIOFlushSync(void *pvUser, void *pStorage)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;
    PPDMASYNCCOMPLETIONTASK pTask;

    LogFlowFunc(("pvUser=%#p pStorage=%#p\n", pvUser, pStorage));

    bool fOld = ASMAtomicXchgBool(&pStorageBackend->fSyncIoPending, true);
    Assert(!fOld); NOREF(fOld);

    int rc = PDMDrvHlpAsyncCompletionEpFlush(pThis->pDrvIns, pStorageBackend->pEndpoint, NULL, &pTask);
    if (RT_FAILURE(rc))
        return rc;

    if (rc == VINF_AIO_TASK_PENDING)
    {
        /* Wait */
        LogFlowFunc(("Waiting for flush to complete\n"));
        rc = RTSemEventWait(pStorageBackend->EventSem, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }
    else
        ASMAtomicXchgBool(&pStorageBackend->fSyncIoPending, false);

    return pStorageBackend->rcReqLast;
}

static DECLCALLBACK(int) drvvdAsyncIOReadAsync(void *pvUser, void *pStorage, uint64_t uOffset,
                                               PCRTSGSEG paSegments, size_t cSegments,
                                               size_t cbRead, void *pvCompletion,
                                               void **ppTask)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;

    int rc = PDMDrvHlpAsyncCompletionEpRead(pThis->pDrvIns, pStorageBackend->pEndpoint,
                                            uOffset, paSegments, (unsigned)cSegments, cbRead,
                                            pvCompletion, (PPPDMASYNCCOMPLETIONTASK)ppTask);
    if (rc == VINF_AIO_TASK_PENDING)
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;

    return rc;
}

static DECLCALLBACK(int) drvvdAsyncIOWriteAsync(void *pvUser, void *pStorage, uint64_t uOffset,
                                                PCRTSGSEG paSegments, size_t cSegments,
                                                size_t cbWrite, void *pvCompletion,
                                                void **ppTask)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;

    int rc = PDMDrvHlpAsyncCompletionEpWrite(pThis->pDrvIns, pStorageBackend->pEndpoint,
                                             uOffset, paSegments, (unsigned)cSegments, cbWrite,
                                             pvCompletion, (PPPDMASYNCCOMPLETIONTASK)ppTask);
    if (rc == VINF_AIO_TASK_PENDING)
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;

    return rc;
}

static DECLCALLBACK(int) drvvdAsyncIOFlushAsync(void *pvUser, void *pStorage,
                                                void *pvCompletion, void **ppTask)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;

    int rc = PDMDrvHlpAsyncCompletionEpFlush(pThis->pDrvIns, pStorageBackend->pEndpoint, pvCompletion,
                                             (PPPDMASYNCCOMPLETIONTASK)ppTask);
    if (rc == VINF_AIO_TASK_PENDING)
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;

    return rc;
}

static DECLCALLBACK(int) drvvdAsyncIOGetSize(void *pvUser, void *pStorage, uint64_t *pcbSize)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;

    return PDMDrvHlpAsyncCompletionEpGetSize(pThis->pDrvIns, pStorageBackend->pEndpoint, pcbSize);
}

static DECLCALLBACK(int) drvvdAsyncIOSetSize(void *pvUser, void *pStorage, uint64_t cbSize)
{
    RT_NOREF(pvUser);
    PDRVVDSTORAGEBACKEND pStorageBackend = (PDRVVDSTORAGEBACKEND)pStorage;
    PVBOXDISK pThis = pStorageBackend->pVD;

    return PDMDrvHlpAsyncCompletionEpSetSize(pThis->pDrvIns, pStorageBackend->pEndpoint, cbSize);
}

static DECLCALLBACK(int) drvvdAsyncIOSetAllocationSize(void *pvUser, void *pvStorage, uint64_t cbSize, uint32_t fFlags)
{
    RT_NOREF(pvUser, pvStorage, cbSize, fFlags);
    return VERR_NOT_SUPPORTED;
}

#endif /* VBOX_WITH_PDM_ASYNC_COMPLETION */


/*********************************************************************************************************************************
*   VD Thread Synchronization interface implementation                                                                           *
*********************************************************************************************************************************/

static DECLCALLBACK(int) drvvdThreadStartRead(void *pvUser)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;

    return RTSemRWRequestRead(pThis->MergeLock, RT_INDEFINITE_WAIT);
}

static DECLCALLBACK(int) drvvdThreadFinishRead(void *pvUser)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;

    return RTSemRWReleaseRead(pThis->MergeLock);
}

static DECLCALLBACK(int) drvvdThreadStartWrite(void *pvUser)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;

    return RTSemRWRequestWrite(pThis->MergeLock, RT_INDEFINITE_WAIT);
}

static DECLCALLBACK(int) drvvdThreadFinishWrite(void *pvUser)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;

    return RTSemRWReleaseWrite(pThis->MergeLock);
}


/*********************************************************************************************************************************
*   VD Configuration interface implementation                                                                                    *
*********************************************************************************************************************************/

static DECLCALLBACK(bool) drvvdCfgAreKeysValid(void *pvUser, const char *pszzValid)
{
    PVDCFGNODE      pVdCfgNode = (PVDCFGNODE)pvUser;
    PCPDMDRVHLPR3   pHlp       = pVdCfgNode->pHlp;
    return pHlp->pfnCFGMAreValuesValid(pVdCfgNode->pCfgNode, pszzValid);
}

static DECLCALLBACK(int) drvvdCfgQuerySize(void *pvUser, const char *pszName, size_t *pcb)
{
    PVDCFGNODE      pVdCfgNode = (PVDCFGNODE)pvUser;
    PCPDMDRVHLPR3   pHlp       = pVdCfgNode->pHlp;
    return pHlp->pfnCFGMQuerySize(pVdCfgNode->pCfgNode, pszName, pcb);
}

static DECLCALLBACK(int) drvvdCfgQuery(void *pvUser, const char *pszName, char *pszString, size_t cchString)
{
    PVDCFGNODE      pVdCfgNode = (PVDCFGNODE)pvUser;
    PCPDMDRVHLPR3   pHlp       = pVdCfgNode->pHlp;
    return pHlp->pfnCFGMQueryString(pVdCfgNode->pCfgNode, pszName, pszString, cchString);
}

static DECLCALLBACK(int) drvvdCfgQueryBytes(void *pvUser, const char *pszName, void *ppvData, size_t cbData)
{
    PVDCFGNODE      pVdCfgNode = (PVDCFGNODE)pvUser;
    PCPDMDRVHLPR3   pHlp       = pVdCfgNode->pHlp;
    return pHlp->pfnCFGMQueryBytes(pVdCfgNode->pCfgNode, pszName, ppvData, cbData);
}


/*******************************************************************************
*   VD Crypto interface implementation for the encryption support       *
*******************************************************************************/

static DECLCALLBACK(int) drvvdCryptoKeyRetain(void *pvUser, const char *pszId, const uint8_t **ppbKey, size_t *pcbKey)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;
    int rc = VINF_SUCCESS;

    AssertPtr(pThis->pIfSecKey);
    if (pThis->pIfSecKey)
        rc = pThis->pIfSecKey->pfnKeyRetain(pThis->pIfSecKey, pszId, ppbKey, pcbKey);
    else
        rc = VERR_NOT_SUPPORTED;

    return rc;
}

static DECLCALLBACK(int) drvvdCryptoKeyRelease(void *pvUser, const char *pszId)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;
    int rc = VINF_SUCCESS;

    AssertPtr(pThis->pIfSecKey);
    if (pThis->pIfSecKey)
        rc = pThis->pIfSecKey->pfnKeyRelease(pThis->pIfSecKey, pszId);
    else
        rc = VERR_NOT_SUPPORTED;

    return rc;
}

static DECLCALLBACK(int) drvvdCryptoKeyStorePasswordRetain(void *pvUser, const char *pszId, const char **ppszPassword)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;
    int rc = VINF_SUCCESS;

    AssertPtr(pThis->pIfSecKey);
    if (pThis->pIfSecKey)
        rc = pThis->pIfSecKey->pfnPasswordRetain(pThis->pIfSecKey, pszId, ppszPassword);
    else
        rc = VERR_NOT_SUPPORTED;

    return rc;
}

static DECLCALLBACK(int) drvvdCryptoKeyStorePasswordRelease(void *pvUser, const char *pszId)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser;
    int rc = VINF_SUCCESS;

    AssertPtr(pThis->pIfSecKey);
    if (pThis->pIfSecKey)
        rc = pThis->pIfSecKey->pfnPasswordRelease(pThis->pIfSecKey, pszId);
    else
        rc = VERR_NOT_SUPPORTED;

    return rc;
}

#ifdef VBOX_WITH_INIP


/*********************************************************************************************************************************
*   VD TCP network stack interface implementation - INIP case                                                                    *
*********************************************************************************************************************************/

/**
 * vvl: this structure duplicate meaning of sockaddr,
 * perhaps it'd be better to get rid of it.
 */
typedef union INIPSOCKADDRUNION
{
    struct sockaddr     Addr;
    struct sockaddr_in  Ipv4;
#if LWIP_IPV6
    struct sockaddr_in6 Ipv6;
#endif
} INIPSOCKADDRUNION;

typedef struct INIPSOCKET
{
    int hSock;
} INIPSOCKET, *PINIPSOCKET;

static DECLCALLBACK(int) drvvdINIPFlush(VDSOCKET Sock);

/** @interface_method_impl{VDINTERFACETCPNET,pfnSocketCreate} */
static DECLCALLBACK(int) drvvdINIPSocketCreate(uint32_t fFlags, PVDSOCKET pSock)
{
    PINIPSOCKET pSocketInt = NULL;

    /*
     * The extended select method is not supported because it is impossible to wakeup
     * the thread.
     */
    if (fFlags & VD_INTERFACETCPNET_CONNECT_EXTENDED_SELECT)
        return VERR_NOT_SUPPORTED;

    pSocketInt = (PINIPSOCKET)RTMemAllocZ(sizeof(INIPSOCKET));
    if (pSocketInt)
    {
        pSocketInt->hSock = INT32_MAX;
        *pSock = (VDSOCKET)pSocketInt;
        return VINF_SUCCESS;
    }

    return VERR_NO_MEMORY;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSocketCreate} */
static DECLCALLBACK(int) drvvdINIPSocketDestroy(VDSOCKET Sock)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;

    RTMemFree(pSocketInt);
    return VINF_SUCCESS;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnClientConnect} */
static DECLCALLBACK(int) drvvdINIPClientConnect(VDSOCKET Sock, const char *pszAddress, uint32_t uPort,
                                                RTMSINTERVAL cMillies)
{
    int rc = VINF_SUCCESS;
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;
    int iInetFamily = PF_INET;
    struct in_addr ip;
#if LWIP_IPV6
    ip6_addr_t ip6;
    RT_ZERO(ip6);
#endif

    NOREF(cMillies); /* LwIP doesn't support connect timeout. */
    RT_ZERO(ip); /* Shut up MSC. */

    /* Check whether lwIP is set up in this VM instance. */
    if (!DevINIPConfigured())
    {
        LogRelFunc(("no IP stack\n"));
        return VERR_NET_HOST_UNREACHABLE;
    }
    /* Resolve hostname. As there is no standard resolver for lwIP yet,
     * just accept numeric IP addresses for now. */
#if LWIP_IPV6
    if (inet6_aton(pszAddress, &ip6))
        iInetFamily = PF_INET6;
    else /* concatination with if */
#endif
      if (!lwip_inet_aton(pszAddress, &ip))
    {
        LogRelFunc(("cannot resolve IP %s\n", pszAddress));
        return VERR_NET_HOST_UNREACHABLE;
    }
    /* Create socket and connect. */
    int iSock = lwip_socket(iInetFamily, SOCK_STREAM, 0);
    if (iSock != -1)
    {
        struct sockaddr *pSockAddr = NULL;
        struct sockaddr_in InAddr = {0};
#if LWIP_IPV6
        struct sockaddr_in6 In6Addr = {0};
#endif
        if (iInetFamily == PF_INET)
        {
            InAddr.sin_family = AF_INET;
            InAddr.sin_port = htons(uPort);
            InAddr.sin_addr = ip;
            InAddr.sin_len = sizeof(InAddr);
            pSockAddr = (struct sockaddr *)&InAddr;
        }
#if LWIP_IPV6
        else
        {
            In6Addr.sin6_family = AF_INET6;
            In6Addr.sin6_port = htons(uPort);
            memcpy(&In6Addr.sin6_addr, &ip6, sizeof(ip6));
            In6Addr.sin6_len = sizeof(In6Addr);
            pSockAddr = (struct sockaddr *)&In6Addr;
        }
#endif
        if (   pSockAddr
            && !lwip_connect(iSock, pSockAddr, pSockAddr->sa_len))
        {
            pSocketInt->hSock = iSock;
            return VINF_SUCCESS;
        }
        rc = VERR_NET_CONNECTION_REFUSED; /** @todo real solution needed */
        lwip_close(iSock);
    }
    else
        rc = VERR_NET_CONNECTION_REFUSED; /** @todo real solution needed */
    return rc;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnClientClose} */
static DECLCALLBACK(int) drvvdINIPClientClose(VDSOCKET Sock)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;

    lwip_close(pSocketInt->hSock);
    pSocketInt->hSock = INT32_MAX;
    return VINF_SUCCESS; /** @todo real solution needed */
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnIsClientConnected} */
static DECLCALLBACK(bool) drvvdINIPIsClientConnected(VDSOCKET Sock)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;

    return pSocketInt->hSock != INT32_MAX;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSelectOne} */
static DECLCALLBACK(int) drvvdINIPSelectOne(VDSOCKET Sock, RTMSINTERVAL cMillies)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;
    fd_set fdsetR;
    FD_ZERO(&fdsetR);
    FD_SET((uintptr_t)pSocketInt->hSock, &fdsetR);
    fd_set fdsetE = fdsetR;

    int rc;
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = lwip_select(pSocketInt->hSock + 1, &fdsetR, NULL, &fdsetE, NULL);
    else
    {
        struct timeval timeout;
        timeout.tv_sec = cMillies / 1000;
        timeout.tv_usec = (cMillies % 1000) * 1000;
        rc = lwip_select(pSocketInt->hSock + 1, &fdsetR, NULL, &fdsetE, &timeout);
    }
    if (rc > 0)
        return VINF_SUCCESS;
    if (rc == 0)
        return VERR_TIMEOUT;
    return VERR_NET_CONNECTION_REFUSED; /** @todo real solution needed */
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnRead} */
static DECLCALLBACK(int) drvvdINIPRead(VDSOCKET Sock, void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;

    /* Do params checking */
    if (!pvBuffer || !cbBuffer)
    {
        AssertMsgFailed(("Invalid params\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Read loop.
     * If pcbRead is NULL we have to fill the entire buffer!
     */
    size_t cbRead = 0;
    size_t cbToRead = cbBuffer;
    for (;;)
    {
        /** @todo this clipping here is just in case (the send function
         * needed it, so I added it here, too). Didn't investigate if this
         * really has issues. Better be safe than sorry. */
        ssize_t cbBytesRead = lwip_recv(pSocketInt->hSock, (char *)pvBuffer + cbRead,
                                        RT_MIN(cbToRead, 32768), 0);
        if (cbBytesRead < 0)
            return VERR_NET_CONNECTION_REFUSED; /** @todo real solution */
        if (cbBytesRead == 0 && errno) /** @todo r=bird: lwip_recv will not touch errno on Windows.  This may apply to other hosts as well  */
            return VERR_NET_CONNECTION_REFUSED; /** @todo real solution */
        if (pcbRead)
        {
            /* return partial data */
            *pcbRead = cbBytesRead;
            break;
        }

        /* read more? */
        cbRead += cbBytesRead;
        if (cbRead == cbBuffer)
            break;

        /* next */
        cbToRead = cbBuffer - cbRead;
    }

    return VINF_SUCCESS;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnWrite} */
static DECLCALLBACK(int) drvvdINIPWrite(VDSOCKET Sock, const void *pvBuffer, size_t cbBuffer)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;

    do
    {
        /** @todo lwip send only supports up to 65535 bytes in a single
         * send (stupid limitation buried in the code), so make sure we
         * don't get any wraparounds. This should be moved to DevINIP
         * stack interface once that's implemented. */
        ssize_t cbWritten = lwip_send(pSocketInt->hSock, (void *)pvBuffer,
                                      RT_MIN(cbBuffer, 32768), 0);
        if (cbWritten < 0)
            return VERR_NET_CONNECTION_REFUSED; /** @todo real solution needed */
        AssertMsg(cbBuffer >= (size_t)cbWritten, ("Wrote more than we requested!!! cbWritten=%d cbBuffer=%d\n",
                                                  cbWritten, cbBuffer));
        cbBuffer -= cbWritten;
        pvBuffer = (const char *)pvBuffer + cbWritten;
    } while (cbBuffer);

    return VINF_SUCCESS;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSgWrite} */
static DECLCALLBACK(int) drvvdINIPSgWrite(VDSOCKET Sock, PCRTSGBUF pSgBuf)
{
    int rc = VINF_SUCCESS;

    /* This is an extremely crude emulation, however it's good enough
     * for our iSCSI code. INIP has no sendmsg(). */
    for (unsigned i = 0; i < pSgBuf->cSegs; i++)
    {
        rc = drvvdINIPWrite(Sock, pSgBuf->paSegs[i].pvSeg,
                            pSgBuf->paSegs[i].cbSeg);
        if (RT_FAILURE(rc))
            break;
    }
    if (RT_SUCCESS(rc))
        drvvdINIPFlush(Sock);

    return rc;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnFlush} */
static DECLCALLBACK(int) drvvdINIPFlush(VDSOCKET Sock)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;

    int fFlag = 1;
    lwip_setsockopt(pSocketInt->hSock, IPPROTO_TCP, TCP_NODELAY,
                    (const char *)&fFlag, sizeof(fFlag));
    fFlag = 0;
    lwip_setsockopt(pSocketInt->hSock, IPPROTO_TCP, TCP_NODELAY,
                    (const char *)&fFlag, sizeof(fFlag));
    return VINF_SUCCESS;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSetSendCoalescing} */
static DECLCALLBACK(int) drvvdINIPSetSendCoalescing(VDSOCKET Sock, bool fEnable)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;

    int fFlag = fEnable ? 0 : 1;
    lwip_setsockopt(pSocketInt->hSock, IPPROTO_TCP, TCP_NODELAY,
                    (const char *)&fFlag, sizeof(fFlag));
    return VINF_SUCCESS;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnGetLocalAddress} */
static DECLCALLBACK(int) drvvdINIPGetLocalAddress(VDSOCKET Sock, PRTNETADDR pAddr)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;
    INIPSOCKADDRUNION u;
    socklen_t cbAddr = sizeof(u);
    RT_ZERO(u);
    if (!lwip_getsockname(pSocketInt->hSock, &u.Addr, &cbAddr))
    {
        /*
         * Convert the address.
         */
        if (   cbAddr == sizeof(struct sockaddr_in)
            && u.Addr.sa_family == AF_INET)
        {
            RT_ZERO(*pAddr);
            pAddr->enmType      = RTNETADDRTYPE_IPV4;
            pAddr->uPort        = RT_N2H_U16(u.Ipv4.sin_port);
            pAddr->uAddr.IPv4.u = u.Ipv4.sin_addr.s_addr;
        }
#if LWIP_IPV6
        else if (   cbAddr == sizeof(struct sockaddr_in6)
            && u.Addr.sa_family == AF_INET6)
        {
            RT_ZERO(*pAddr);
            pAddr->enmType      = RTNETADDRTYPE_IPV6;
            pAddr->uPort        = RT_N2H_U16(u.Ipv6.sin6_port);
            memcpy(&pAddr->uAddr.IPv6, &u.Ipv6.sin6_addr, sizeof(RTNETADDRIPV6));
        }
#endif
        else
            return VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED;
        return VINF_SUCCESS;
    }
    return VERR_NET_OPERATION_NOT_SUPPORTED;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnGetPeerAddress} */
static DECLCALLBACK(int) drvvdINIPGetPeerAddress(VDSOCKET Sock, PRTNETADDR pAddr)
{
    PINIPSOCKET pSocketInt = (PINIPSOCKET)Sock;
    INIPSOCKADDRUNION u;
    socklen_t cbAddr = sizeof(u);
    RT_ZERO(u);
    if (!lwip_getpeername(pSocketInt->hSock, &u.Addr, &cbAddr))
    {
        /*
         * Convert the address.
         */
        if (   cbAddr == sizeof(struct sockaddr_in)
            && u.Addr.sa_family == AF_INET)
        {
            RT_ZERO(*pAddr);
            pAddr->enmType      = RTNETADDRTYPE_IPV4;
            pAddr->uPort        = RT_N2H_U16(u.Ipv4.sin_port);
            pAddr->uAddr.IPv4.u = u.Ipv4.sin_addr.s_addr;
        }
#if LWIP_IPV6
        else if (   cbAddr == sizeof(struct sockaddr_in6)
                 && u.Addr.sa_family == AF_INET6)
        {
            RT_ZERO(*pAddr);
            pAddr->enmType      = RTNETADDRTYPE_IPV6;
            pAddr->uPort        = RT_N2H_U16(u.Ipv6.sin6_port);
            memcpy(&pAddr->uAddr.IPv6, &u.Ipv6.sin6_addr, sizeof(RTNETADDRIPV6));
        }
#endif
        else
            return VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED;
        return VINF_SUCCESS;
    }
    return VERR_NET_OPERATION_NOT_SUPPORTED;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnSelectOneEx} */
static DECLCALLBACK(int) drvvdINIPSelectOneEx(VDSOCKET Sock, uint32_t fEvents, uint32_t *pfEvents, RTMSINTERVAL cMillies)
{
    RT_NOREF(Sock, fEvents, pfEvents, cMillies);
    AssertMsgFailed(("Not supported!\n"));
    return VERR_NOT_SUPPORTED;
}

/** @interface_method_impl{VDINTERFACETCPNET,pfnPoke} */
static DECLCALLBACK(int) drvvdINIPPoke(VDSOCKET Sock)
{
    RT_NOREF(Sock);
    AssertMsgFailed(("Not supported!\n"));
    return VERR_NOT_SUPPORTED;
}

#endif /* VBOX_WITH_INIP */


/**
 * Checks the prerequisites for encrypted I/O.
 *
 * @returns VBox status code.
 * @param   pThis     The VD driver instance data.
 * @param   fSetError Flag whether to set a runtime error.
 */
static int drvvdKeyCheckPrereqs(PVBOXDISK pThis, bool fSetError)
{
    if (   pThis->CfgCrypto.pCfgNode
        && !pThis->pIfSecKey)
    {
        AssertPtr(pThis->pIfSecKeyHlp);
        pThis->pIfSecKeyHlp->pfnKeyMissingNotify(pThis->pIfSecKeyHlp);

        if (fSetError)
        {
            int rc = PDMDrvHlpVMSetRuntimeError(pThis->pDrvIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DrvVD_DEKMISSING",
                                                N_("VD: The DEK for this disk is missing"));
            AssertRC(rc);
        }
        return VERR_VD_DEK_MISSING;
    }

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Media interface methods                                                                                                      *
*********************************************************************************************************************************/

/** @interface_method_impl{PDMIMEDIA,pfnRead} */
static DECLCALLBACK(int) drvvdRead(PPDMIMEDIA pInterface,
                                   uint64_t off, void *pvBuf, size_t cbRead)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("off=%#llx pvBuf=%p cbRead=%d\n", off, pvBuf, cbRead));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        return VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    rc = drvvdKeyCheckPrereqs(pThis, true /* fSetError */);
    if (RT_FAILURE(rc))
        return rc;

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsRead);

    if (!pThis->fBootAccelActive)
        rc = VDRead(pThis->pDisk, off, pvBuf, cbRead);
    else
    {
        /* Can we serve the request from the buffer? */
        if (   off >= pThis->offDisk
            && off - pThis->offDisk < pThis->cbDataValid)
        {
            size_t cbToCopy = RT_MIN(cbRead, pThis->offDisk + pThis->cbDataValid - off);

            memcpy(pvBuf, pThis->pbData + (off - pThis->offDisk), cbToCopy);
            cbRead -= cbToCopy;
            off    += cbToCopy;
            pvBuf   = (char *)pvBuf + cbToCopy;
        }

        if (   cbRead > 0
            && cbRead < pThis->cbBootAccelBuffer)
        {
            /* Increase request to the buffer size and read. */
            pThis->cbDataValid = RT_MIN(pThis->cbDisk - off, pThis->cbBootAccelBuffer);
            pThis->offDisk = off;
            rc = VDRead(pThis->pDisk, off, pThis->pbData, pThis->cbDataValid);
            if (RT_FAILURE(rc))
                pThis->cbDataValid = 0;
            else
                memcpy(pvBuf, pThis->pbData, cbRead);
        }
        else if (cbRead >= pThis->cbBootAccelBuffer)
        {
            pThis->fBootAccelActive = false; /* Deactiviate */
        }
    }

    if (RT_SUCCESS(rc))
    {
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
        STAM_REL_COUNTER_ADD(&pThis->StatBytesRead, cbRead);
        Log2(("%s: off=%#llx pvBuf=%p cbRead=%d\n%.*Rhxd\n", __FUNCTION__,
              off, pvBuf, cbRead, cbRead, pvBuf));
    }
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnRead} */
static DECLCALLBACK(int) drvvdReadPcBios(PPDMIMEDIA pInterface,
                                         uint64_t off, void *pvBuf, size_t cbRead)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("off=%#llx pvBuf=%p cbRead=%d\n", off, pvBuf, cbRead));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        return VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    if (   pThis->CfgCrypto.pCfgNode
        && !pThis->pIfSecKey)
        return VERR_VD_DEK_MISSING;

    if (!pThis->fBootAccelActive)
        rc = VDRead(pThis->pDisk, off, pvBuf, cbRead);
    else
    {
        /* Can we serve the request from the buffer? */
        if (   off >= pThis->offDisk
            && off - pThis->offDisk < pThis->cbDataValid)
        {
            size_t cbToCopy = RT_MIN(cbRead, pThis->offDisk + pThis->cbDataValid - off);

            memcpy(pvBuf, pThis->pbData + (off - pThis->offDisk), cbToCopy);
            cbRead -= cbToCopy;
            off    += cbToCopy;
            pvBuf   = (char *)pvBuf + cbToCopy;
        }

        if (   cbRead > 0
            && cbRead < pThis->cbBootAccelBuffer)
        {
            /* Increase request to the buffer size and read. */
            pThis->cbDataValid = RT_MIN(pThis->cbDisk - off, pThis->cbBootAccelBuffer);
            pThis->offDisk = off;
            rc = VDRead(pThis->pDisk, off, pThis->pbData, pThis->cbDataValid);
            if (RT_FAILURE(rc))
                pThis->cbDataValid = 0;
            else
                memcpy(pvBuf, pThis->pbData, cbRead);
        }
        else if (cbRead >= pThis->cbBootAccelBuffer)
        {
            pThis->fBootAccelActive = false; /* Deactiviate */
        }
    }

    if (RT_SUCCESS(rc))
        Log2(("%s: off=%#llx pvBuf=%p cbRead=%d\n%.*Rhxd\n", __FUNCTION__,
              off, pvBuf, cbRead, cbRead, pvBuf));
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnWrite} */
static DECLCALLBACK(int) drvvdWrite(PPDMIMEDIA pInterface,
                                    uint64_t off, const void *pvBuf,
                                    size_t cbWrite)
{
    LogFlowFunc(("off=%#llx pvBuf=%p cbWrite=%d\n", off, pvBuf, cbWrite));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    Log2(("%s: off=%#llx pvBuf=%p cbWrite=%d\n%.*Rhxd\n", __FUNCTION__,
          off, pvBuf, cbWrite, cbWrite, pvBuf));

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        return VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    int rc = drvvdKeyCheckPrereqs(pThis, true /* fSetError */);
    if (RT_FAILURE(rc))
        return rc;

    /* Invalidate any buffer if boot acceleration is enabled. */
    if (pThis->fBootAccelActive)
    {
        pThis->cbDataValid = 0;
        pThis->offDisk     = 0;
    }

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsWrite);

    rc = VDWrite(pThis->pDisk, off, pvBuf, cbWrite);
#ifdef VBOX_PERIODIC_FLUSH
    if (pThis->cbFlushInterval)
    {
        pThis->cbDataWritten += (uint32_t)cbWrite;
        if (pThis->cbDataWritten > pThis->cbFlushInterval)
        {
            pThis->cbDataWritten = 0;
            VDFlush(pThis->pDisk);
        }
    }
#endif /* VBOX_PERIODIC_FLUSH */

    if (RT_SUCCESS(rc))
    {
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
        STAM_REL_COUNTER_ADD(&pThis->StatBytesWritten, cbWrite);
    }
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnFlush} */
static DECLCALLBACK(int) drvvdFlush(PPDMIMEDIA pInterface)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        return VERR_PDM_MEDIA_NOT_MOUNTED;
    }

#ifdef VBOX_IGNORE_FLUSH
    if (pThis->fIgnoreFlush)
        return VINF_SUCCESS;
#endif /* VBOX_IGNORE_FLUSH */

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsFlush);

    int rc = VDFlush(pThis->pDisk);
    if (RT_SUCCESS(rc))
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnMerge} */
static DECLCALLBACK(int) drvvdMerge(PPDMIMEDIA pInterface,
                                    PFNSIMPLEPROGRESS pfnProgress,
                                    void *pvUser)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VINF_SUCCESS;

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        return VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    /* Note: There is an unavoidable race between destruction and another
     * thread invoking this function. This is handled safely and gracefully by
     * atomically invalidating the lock handle in drvvdDestruct. */
    int rc2 = RTSemFastMutexRequest(pThis->MergeCompleteMutex);
    AssertRC(rc2);
    if (RT_SUCCESS(rc2) && pThis->fMergePending)
    {
        /* Take shortcut: PFNSIMPLEPROGRESS is exactly the same type as
         * PFNVDPROGRESS, so there's no need for a conversion function. */
        /** @todo maybe introduce a conversion which limits update frequency. */
        PVDINTERFACE pVDIfsOperation = NULL;
        VDINTERFACEPROGRESS VDIfProgress;
        VDIfProgress.pfnProgress  = pfnProgress;
        rc2 = VDInterfaceAdd(&VDIfProgress.Core, "DrvVD_VDIProgress", VDINTERFACETYPE_PROGRESS,
                             pvUser, sizeof(VDINTERFACEPROGRESS), &pVDIfsOperation);
        AssertRC(rc2);
        pThis->fMergePending = false;
        rc = VDMerge(pThis->pDisk, pThis->uMergeSource,
                     pThis->uMergeTarget, pVDIfsOperation);
    }
    rc2 = RTSemFastMutexRelease(pThis->MergeCompleteMutex);
    AssertRC(rc2);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnSetSecKeyIf} */
static DECLCALLBACK(int) drvvdSetSecKeyIf(PPDMIMEDIA pInterface, PPDMISECKEY pIfSecKey, PPDMISECKEYHLP pIfSecKeyHlp)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    int rc = VINF_SUCCESS;

    if (pThis->CfgCrypto.pCfgNode)
    {
        PVDINTERFACE pVDIfFilter = NULL;

        pThis->pIfSecKeyHlp = pIfSecKeyHlp;

        if (   pThis->pIfSecKey
            && !pIfSecKey)
        {
            /* Unload the crypto filter first to make sure it doesn't access the keys anymore. */
            rc = VDFilterRemove(pThis->pDisk, VD_FILTER_FLAGS_DEFAULT);
            AssertRC(rc);

            pThis->pIfSecKey = NULL;
        }

        if (   pIfSecKey
            && RT_SUCCESS(rc))
        {
            pThis->pIfSecKey = pIfSecKey;

            rc = VDInterfaceAdd(&pThis->VDIfCfg.Core, "DrvVD_Config", VDINTERFACETYPE_CONFIG,
                                &pThis->CfgCrypto, sizeof(VDINTERFACECONFIG), &pVDIfFilter);
            AssertRC(rc);

            rc = VDInterfaceAdd(&pThis->VDIfCrypto.Core, "DrvVD_Crypto", VDINTERFACETYPE_CRYPTO,
                                pThis, sizeof(VDINTERFACECRYPTO), &pVDIfFilter);
            AssertRC(rc);

            /* Load the crypt filter plugin. */
            rc = VDFilterAdd(pThis->pDisk, "CRYPT", VD_FILTER_FLAGS_DEFAULT, pVDIfFilter);
            if (RT_FAILURE(rc))
                pThis->pIfSecKey = NULL;
        }
    }
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetSize} */
static DECLCALLBACK(uint64_t) drvvdGetSize(PPDMIMEDIA pInterface)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
        return 0;

    uint64_t cb = VDGetSize(pThis->pDisk, VD_LAST_IMAGE);
    LogFlowFunc(("returns %#llx (%llu)\n", cb, cb));
    return cb;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetSectorSize} */
static DECLCALLBACK(uint32_t) drvvdGetSectorSize(PPDMIMEDIA pInterface)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
        return 0;

    uint32_t cb = VDGetSectorSize(pThis->pDisk, VD_LAST_IMAGE);
    LogFlowFunc(("returns %u\n", cb));
    return cb;
}

/** @interface_method_impl{PDMIMEDIA,pfnIsReadOnly} */
static DECLCALLBACK(bool) drvvdIsReadOnly(PPDMIMEDIA pInterface)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
        return false;

    bool f = VDIsReadOnly(pThis->pDisk);
    LogFlowFunc(("returns %d\n", f));
    return f;
}

/** @interface_method_impl{PDMIMEDIA,pfnIsNonRotational} */
static DECLCALLBACK(bool) drvvdIsNonRotational(PPDMIMEDIA pInterface)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    return pThis->fNonRotational;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosGetPCHSGeometry} */
static DECLCALLBACK(int) drvvdBiosGetPCHSGeometry(PPDMIMEDIA pInterface,
                                                  PPDMMEDIAGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    VDGEOMETRY geo;

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
        return VERR_PDM_MEDIA_NOT_MOUNTED;

    /*
     * Use configured/cached values if present.
     */
    if (    pThis->PCHSGeometry.cCylinders > 0
        &&  pThis->PCHSGeometry.cHeads > 0
        &&  pThis->PCHSGeometry.cSectors > 0)
    {
        *pPCHSGeometry = pThis->PCHSGeometry;
        LogFlow(("%s: returns VINF_SUCCESS {%d,%d,%d}\n", __FUNCTION__, pThis->PCHSGeometry.cCylinders, pThis->PCHSGeometry.cHeads, pThis->PCHSGeometry.cSectors));
        return VINF_SUCCESS;
    }

    int rc = VDGetPCHSGeometry(pThis->pDisk, VD_LAST_IMAGE, &geo);
    if (RT_SUCCESS(rc))
    {
        pPCHSGeometry->cCylinders = geo.cCylinders;
        pPCHSGeometry->cHeads = geo.cHeads;
        pPCHSGeometry->cSectors = geo.cSectors;
        pThis->PCHSGeometry = *pPCHSGeometry;
    }
    else
    {
        LogFunc(("geometry not available.\n"));
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    }
    LogFlowFunc(("returns %Rrc (CHS=%d/%d/%d)\n",
                 rc, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosSetPCHSGeometry} */
static DECLCALLBACK(int) drvvdBiosSetPCHSGeometry(PPDMIMEDIA pInterface,
                                                  PCPDMMEDIAGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("CHS=%d/%d/%d\n",
                 pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    VDGEOMETRY geo;

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        return VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    geo.cCylinders = pPCHSGeometry->cCylinders;
    geo.cHeads = pPCHSGeometry->cHeads;
    geo.cSectors = pPCHSGeometry->cSectors;
    int rc = VDSetPCHSGeometry(pThis->pDisk, VD_LAST_IMAGE, &geo);
    if (rc == VERR_VD_GEOMETRY_NOT_SET)
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    if (RT_SUCCESS(rc))
        pThis->PCHSGeometry = *pPCHSGeometry;
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosGetLCHSGeometry} */
static DECLCALLBACK(int) drvvdBiosGetLCHSGeometry(PPDMIMEDIA pInterface,
                                                  PPDMMEDIAGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    VDGEOMETRY geo;

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
        return VERR_PDM_MEDIA_NOT_MOUNTED;

    /*
     * Use configured/cached values if present.
     */
    if (    pThis->LCHSGeometry.cCylinders > 0
        &&  pThis->LCHSGeometry.cHeads > 0
        &&  pThis->LCHSGeometry.cSectors > 0)
    {
        *pLCHSGeometry = pThis->LCHSGeometry;
        LogFlow(("%s: returns VINF_SUCCESS {%d,%d,%d}\n", __FUNCTION__, pThis->LCHSGeometry.cCylinders, pThis->LCHSGeometry.cHeads, pThis->LCHSGeometry.cSectors));
        return VINF_SUCCESS;
    }

    int rc = VDGetLCHSGeometry(pThis->pDisk, VD_LAST_IMAGE, &geo);
    if (RT_SUCCESS(rc))
    {
        pLCHSGeometry->cCylinders = geo.cCylinders;
        pLCHSGeometry->cHeads = geo.cHeads;
        pLCHSGeometry->cSectors = geo.cSectors;
        pThis->LCHSGeometry = *pLCHSGeometry;
    }
    else
    {
        LogFunc(("geometry not available.\n"));
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    }
    LogFlowFunc(("returns %Rrc (CHS=%d/%d/%d)\n",
                 rc, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosSetLCHSGeometry} */
static DECLCALLBACK(int) drvvdBiosSetLCHSGeometry(PPDMIMEDIA pInterface,
                                                  PCPDMMEDIAGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("CHS=%d/%d/%d\n",
                 pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    VDGEOMETRY geo;

    /*
     * Check the state.
     */
    if (!pThis->pDisk)
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        return VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    geo.cCylinders = pLCHSGeometry->cCylinders;
    geo.cHeads = pLCHSGeometry->cHeads;
    geo.cSectors = pLCHSGeometry->cSectors;
    int rc = VDSetLCHSGeometry(pThis->pDisk, VD_LAST_IMAGE, &geo);
    if (rc == VERR_VD_GEOMETRY_NOT_SET)
        rc = VERR_PDM_GEOMETRY_NOT_SET;
    if (RT_SUCCESS(rc))
        pThis->LCHSGeometry = *pLCHSGeometry;
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnBiosIsVisible} */
static DECLCALLBACK(bool) drvvdBiosIsVisible(PPDMIMEDIA pInterface)
{
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    LogFlow(("drvvdBiosIsVisible: returns %d\n", pThis->fBiosVisible));
    return pThis->fBiosVisible;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetType} */
static DECLCALLBACK(PDMMEDIATYPE) drvvdGetType(PPDMIMEDIA pInterface)
{
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    LogFlow(("drvvdBiosIsVisible: returns %d\n", pThis->fBiosVisible));
    return pThis->enmType;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetUuid} */
static DECLCALLBACK(int) drvvdGetUuid(PPDMIMEDIA pInterface, PRTUUID pUuid)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    /*
     * Copy the uuid.
     */
    *pUuid = pThis->Uuid;
    LogFlowFunc(("returns {%RTuuid}\n", pUuid));
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMEDIA,pfnDiscard} */
static DECLCALLBACK(int) drvvdDiscard(PPDMIMEDIA pInterface, PCRTRANGE paRanges, unsigned cRanges)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsDiscard);

    int rc = VDDiscardRanges(pThis->pDisk, paRanges, cRanges);
    if (RT_SUCCESS(rc))
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);
    else
        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnGetRegionCount} */
static DECLCALLBACK(uint32_t) drvvdGetRegionCount(PPDMIMEDIA pInterface)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);
    uint32_t cRegions = 0;

    if (pThis->pDisk)
    {
        if (!pThis->pRegionList)
        {
            int rc = VDQueryRegions(pThis->pDisk, VD_LAST_IMAGE, VD_REGION_LIST_F_LOC_SIZE_BLOCKS,
                                    &pThis->pRegionList);
            if (RT_SUCCESS(rc))
                cRegions = pThis->pRegionList->cRegions;
        }
        else
            cRegions = pThis->pRegionList->cRegions;
    }

    LogFlowFunc(("returns %u\n", cRegions));
    return cRegions;
}

/** @interface_method_impl{PDMIMEDIA,pfnQueryRegionProperties} */
static DECLCALLBACK(int) drvvdQueryRegionProperties(PPDMIMEDIA pInterface, uint32_t uRegion, uint64_t *pu64LbaStart,
                                                    uint64_t *pcBlocks, uint64_t *pcbBlock,
                                                    PVDREGIONDATAFORM penmDataForm)
{
    LogFlowFunc(("\n"));
    int rc = VINF_SUCCESS;
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    if (   pThis->pRegionList
        && uRegion < pThis->pRegionList->cRegions)
    {
        PCVDREGIONDESC pRegion = &pThis->pRegionList->aRegions[uRegion];

        if (pu64LbaStart)
            *pu64LbaStart = pRegion->offRegion;
        if (pcBlocks)
            *pcBlocks = pRegion->cRegionBlocksOrBytes;
        if (pcbBlock)
            *pcbBlock = pRegion->cbBlock;
        if (penmDataForm)
            *penmDataForm = pRegion->enmDataForm;
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @interface_method_impl{PDMIMEDIA,pfnQueryRegionPropertiesForLba} */
static DECLCALLBACK(int) drvvdQueryRegionPropertiesForLba(PPDMIMEDIA pInterface, uint64_t u64LbaStart,
                                                          uint32_t *puRegion, uint64_t *pcBlocks,
                                                          uint64_t *pcbBlock, PVDREGIONDATAFORM penmDataForm)
{
    LogFlowFunc(("\n"));
    int rc = VINF_SUCCESS;
    PVBOXDISK pThis = PDMIMEDIA_2_VBOXDISK(pInterface);

    if (!pThis->pRegionList)
        rc = VDQueryRegions(pThis->pDisk, VD_LAST_IMAGE, VD_REGION_LIST_F_LOC_SIZE_BLOCKS,
                            &pThis->pRegionList);

    if (RT_SUCCESS(rc))
    {
        rc = VERR_NOT_FOUND;

        for (uint32_t i = 0; i < pThis->pRegionList->cRegions; i++)
        {
            PCVDREGIONDESC pRegion = &pThis->pRegionList->aRegions[i];
            if (   pRegion->offRegion <= u64LbaStart
                && pRegion->offRegion + pRegion->cRegionBlocksOrBytes > u64LbaStart)
            {
                uint64_t offRegion = u64LbaStart - pRegion->offRegion;

                if (puRegion)
                    *puRegion = i;
                if (pcBlocks)
                    *pcBlocks = pRegion->cRegionBlocksOrBytes - offRegion;
                if (pcbBlock)
                    *pcbBlock = pRegion->cbBlock;
                if (penmDataForm)
                    *penmDataForm = pRegion->enmDataForm;

                rc = VINF_SUCCESS;
            }
        }
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/* -=-=-=-=- IMount -=-=-=-=- */

/** @interface_method_impl{PDMIMOUNT,pfnUnmount} */
static DECLCALLBACK(int) drvvdUnmount(PPDMIMOUNT pInterface, bool fForce, bool fEject)
{
    RT_NOREF(fEject);
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMount);

    /*
     * Validate state.
     */
    if (!pThis->pDisk)
    {
        Log(("drvvdUnmount: Not mounted\n"));
        return VERR_PDM_MEDIA_NOT_MOUNTED;
    }
    if (pThis->fLocked && !fForce)
    {
        Log(("drvvdUnmount: Locked\n"));
        return VERR_PDM_MEDIA_LOCKED;
    }

    /* Media is no longer locked even if it was previously. */
    pThis->fLocked = false;
    drvvdPowerOffOrDestructOrUnmount(pThis->pDrvIns);

    /*
     * Notify driver/device above us.
     */
    if (pThis->pDrvMountNotify)
        pThis->pDrvMountNotify->pfnUnmountNotify(pThis->pDrvMountNotify);
    Log(("drvblockUnmount: success\n"));
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMIMOUNT,pfnIsMounted} */
static DECLCALLBACK(bool) drvvdIsMounted(PPDMIMOUNT pInterface)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMount);
    return pThis->pDisk != NULL;
}

/** @interface_method_impl{PDMIMOUNT,pfnLock} */
static DECLCALLBACK(int) drvvdLock(PPDMIMOUNT pInterface)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMount);
    Log(("drvblockLock: %d -> %d\n", pThis->fLocked, true));
    pThis->fLocked = true;
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMOUNT,pfnUnlock} */
static DECLCALLBACK(int) drvvdUnlock(PPDMIMOUNT pInterface)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMount);
    Log(("drvblockUnlock: %d -> %d\n", pThis->fLocked, false));
    pThis->fLocked = false;
    return VINF_SUCCESS;
}

/** @interface_method_impl{PDMIMOUNT,pfnIsLocked} */
static DECLCALLBACK(bool) drvvdIsLocked(PPDMIMOUNT pInterface)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMount);
    return pThis->fLocked;
}


static DECLCALLBACK(void) drvvdBlkCacheReqComplete(void *pvUser1, void *pvUser2, int rcReq)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser1;

    AssertPtr(pThis->pBlkCache);
    PDMDrvHlpBlkCacheIoXferComplete(pThis->pDrvIns, pThis->pBlkCache, (PPDMBLKCACHEIOXFER)pvUser2, rcReq);
}


/** @copydoc FNPDMBLKCACHEXFERCOMPLETEDRV */
static DECLCALLBACK(void) drvvdBlkCacheXferCompleteIoReq(PPDMDRVINS pDrvIns, void *pvUser, int rc)
{
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);

    drvvdMediaExIoReqCompleteWorker(pThis, (PPDMMEDIAEXIOREQINT)pvUser, rc, true /* fUpNotify */);
}

/** @copydoc FNPDMBLKCACHEXFERENQUEUEDRV */
static DECLCALLBACK(int) drvvdBlkCacheXferEnqueue(PPDMDRVINS pDrvIns,
                                                  PDMBLKCACHEXFERDIR enmXferDir,
                                                  uint64_t off, size_t cbXfer,
                                                  PCRTSGBUF pSgBuf, PPDMBLKCACHEIOXFER hIoXfer)
{
    int rc = VINF_SUCCESS;
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);

    Assert (!pThis->CfgCrypto.pCfgNode);

    switch (enmXferDir)
    {
        case PDMBLKCACHEXFERDIR_READ:
            rc = VDAsyncRead(pThis->pDisk, off, cbXfer, pSgBuf, drvvdBlkCacheReqComplete,
                             pThis, hIoXfer);
            break;
        case PDMBLKCACHEXFERDIR_WRITE:
            rc = VDAsyncWrite(pThis->pDisk, off, cbXfer, pSgBuf, drvvdBlkCacheReqComplete,
                              pThis, hIoXfer);
            break;
        case PDMBLKCACHEXFERDIR_FLUSH:
            rc = VDAsyncFlush(pThis->pDisk, drvvdBlkCacheReqComplete, pThis, hIoXfer);
            break;
        default:
            AssertMsgFailed(("Invalid transfer type %d\n", enmXferDir));
            rc = VERR_INVALID_PARAMETER;
    }

    if (rc == VINF_VD_ASYNC_IO_FINISHED)
        PDMDrvHlpBlkCacheIoXferComplete(pThis->pDrvIns, pThis->pBlkCache, hIoXfer, VINF_SUCCESS);
    else if (RT_FAILURE(rc) && rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
        PDMDrvHlpBlkCacheIoXferComplete(pThis->pDrvIns, pThis->pBlkCache, hIoXfer, rc);

    return VINF_SUCCESS;
}

/** @copydoc FNPDMBLKCACHEXFERENQUEUEDISCARDDRV */
static DECLCALLBACK(int) drvvdBlkCacheXferEnqueueDiscard(PPDMDRVINS pDrvIns, PCRTRANGE paRanges,
                                                         unsigned cRanges, PPDMBLKCACHEIOXFER hIoXfer)
{
    int rc = VINF_SUCCESS;
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);

    rc = VDAsyncDiscardRanges(pThis->pDisk, paRanges, cRanges,
                              drvvdBlkCacheReqComplete, pThis, hIoXfer);

    if (rc == VINF_VD_ASYNC_IO_FINISHED)
        PDMDrvHlpBlkCacheIoXferComplete(pThis->pDrvIns, pThis->pBlkCache, hIoXfer, VINF_SUCCESS);
    else if (RT_FAILURE(rc) && rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
        PDMDrvHlpBlkCacheIoXferComplete(pThis->pDrvIns, pThis->pBlkCache, hIoXfer, rc);

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Extended media interface methods                                                                                             *
*********************************************************************************************************************************/

static void drvvdMediaExIoReqWarningDiskFull(PPDMDRVINS pDrvIns)
{
    int rc;
    LogRel(("VD#%u: Host disk full\n", pDrvIns->iInstance));
    rc = PDMDrvHlpVMSetRuntimeError(pDrvIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DrvVD_DISKFULL",
                                    N_("Host system reported disk full. VM execution is suspended. You can resume after freeing some space"));
    AssertRC(rc);
}

static void drvvdMediaExIoReqWarningFileTooBig(PPDMDRVINS pDrvIns)
{
    int rc;
    LogRel(("VD#%u: File too big\n", pDrvIns->iInstance));
    rc = PDMDrvHlpVMSetRuntimeError(pDrvIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DrvVD_FILETOOBIG",
                                    N_("Host system reported that the file size limit of the host file system has been exceeded. VM execution is suspended. You need to move your virtual hard disk to a filesystem which allows bigger files"));
    AssertRC(rc);
}

static void drvvdMediaExIoReqWarningISCSI(PPDMDRVINS pDrvIns)
{
    int rc;
    LogRel(("VD#%u: iSCSI target unavailable\n", pDrvIns->iInstance));
    rc = PDMDrvHlpVMSetRuntimeError(pDrvIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DrvVD_ISCSIDOWN",
                                    N_("The iSCSI target has stopped responding. VM execution is suspended. You can resume when it is available again"));
    AssertRC(rc);
}

static void drvvdMediaExIoReqWarningFileStale(PPDMDRVINS pDrvIns)
{
    int rc;
    LogRel(("VD#%u: File handle became stale\n", pDrvIns->iInstance));
    rc = PDMDrvHlpVMSetRuntimeError(pDrvIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DrvVD_ISCSIDOWN",
                                    N_("The file became stale (often due to a restarted NFS server). VM execution is suspended. You can resume when it is available again"));
    AssertRC(rc);
}

static void drvvdMediaExIoReqWarningDekMissing(PPDMDRVINS pDrvIns)
{
    LogRel(("VD#%u: DEK is missing\n", pDrvIns->iInstance));
    int rc = PDMDrvHlpVMSetRuntimeError(pDrvIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DrvVD_DEKMISSING",
                                        N_("VD: The DEK for this disk is missing"));
    AssertRC(rc);
}

/**
 * Checks whether a given status code indicates a recoverable error
 * suspending the VM if it is.
 *
 * @returns Flag indicating whether the status code is a recoverable error
 *          (full disk, broken network connection).
 * @param   pThis     VBox disk container instance data.
 * @param   rc        Status code to check.
 */
static bool drvvdMediaExIoReqIsRedoSetWarning(PVBOXDISK pThis, int rc)
{
    if (rc == VERR_DISK_FULL)
    {
        if (ASMAtomicCmpXchgBool(&pThis->fRedo, true, false))
            drvvdMediaExIoReqWarningDiskFull(pThis->pDrvIns);
        return true;
    }
    if (rc == VERR_FILE_TOO_BIG)
    {
        if (ASMAtomicCmpXchgBool(&pThis->fRedo, true, false))
            drvvdMediaExIoReqWarningFileTooBig(pThis->pDrvIns);
        return true;
    }
    if (rc == VERR_BROKEN_PIPE || rc == VERR_NET_CONNECTION_REFUSED)
    {
        /* iSCSI connection abort (first error) or failure to reestablish
         * connection (second error). Pause VM. On resume we'll retry. */
        if (ASMAtomicCmpXchgBool(&pThis->fRedo, true, false))
            drvvdMediaExIoReqWarningISCSI(pThis->pDrvIns);
        return true;
    }
    if (rc == VERR_STALE_FILE_HANDLE)
    {
        if (ASMAtomicCmpXchgBool(&pThis->fRedo, true, false))
            drvvdMediaExIoReqWarningFileStale(pThis->pDrvIns);
        return true;
    }
    if (rc == VERR_VD_DEK_MISSING)
    {
        /* Error message already set. */
        if (ASMAtomicCmpXchgBool(&pThis->fRedo, true, false))
            drvvdMediaExIoReqWarningDekMissing(pThis->pDrvIns);
        return true;
    }

    return false;
}

/**
 * Syncs the memory buffers between the I/O request allocator and the internal buffer.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to sync.
 * @param   fToIoBuf  Flag indicating the sync direction.
 *                    true to copy data from the allocators buffer to our internal buffer.
 *                    false for the other direction.
 */
DECLINLINE(int) drvvdMediaExIoReqBufSync(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, bool fToIoBuf)
{
    int rc = VINF_SUCCESS;

    Assert(pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE);
    Assert(pIoReq->ReadWrite.cbIoBuf > 0);

    if (!pIoReq->ReadWrite.fDirectBuf)
    {
        /* Make sure the buffer is reset. */
        RTSgBufReset(&pIoReq->ReadWrite.IoBuf.SgBuf);

        size_t const offSrc = pIoReq->ReadWrite.cbReq - pIoReq->ReadWrite.cbReqLeft;
        Assert((uint32_t)offSrc == offSrc);
        if (fToIoBuf)
            rc = pThis->pDrvMediaExPort->pfnIoReqCopyToBuf(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0], (uint32_t)offSrc,
                                                           &pIoReq->ReadWrite.IoBuf.SgBuf,
                                                           RT_MIN(pIoReq->ReadWrite.cbIoBuf, pIoReq->ReadWrite.cbReqLeft));
        else
            rc = pThis->pDrvMediaExPort->pfnIoReqCopyFromBuf(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0], (uint32_t)offSrc,
                                                             &pIoReq->ReadWrite.IoBuf.SgBuf,
                                                             (uint32_t)RT_MIN(pIoReq->ReadWrite.cbIoBuf, pIoReq->ReadWrite.cbReqLeft));

        RTSgBufReset(&pIoReq->ReadWrite.IoBuf.SgBuf);
    }
    return rc;
}

/**
 * Hashes the I/O request ID to an index for the allocated I/O request bin.
 */
DECLINLINE(unsigned) drvvdMediaExIoReqIdHash(PDMMEDIAEXIOREQID uIoReqId)
{
    return uIoReqId % DRVVD_VDIOREQ_ALLOC_BINS; /** @todo Find something better? */
}

/**
 * Inserts the given I/O request in to the list of allocated I/O requests.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to insert.
 */
static int drvvdMediaExIoReqInsert(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    int rc = VINF_SUCCESS;
    unsigned idxBin = drvvdMediaExIoReqIdHash(pIoReq->uIoReqId);

    rc = RTSemFastMutexRequest(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    if (RT_SUCCESS(rc))
    {
        /* Search for conflicting I/O request ID. */
        PPDMMEDIAEXIOREQINT pIt;
        RTListForEach(&pThis->aIoReqAllocBins[idxBin].LstIoReqAlloc, pIt, PDMMEDIAEXIOREQINT, NdAllocatedList)
        {
            if (RT_UNLIKELY(   pIt->uIoReqId == pIoReq->uIoReqId
                            && pIt->enmState != VDIOREQSTATE_CANCELED))
            {
                rc = VERR_PDM_MEDIAEX_IOREQID_CONFLICT;
                break;
            }
        }
        if (RT_SUCCESS(rc))
            RTListAppend(&pThis->aIoReqAllocBins[idxBin].LstIoReqAlloc, &pIoReq->NdAllocatedList);
        RTSemFastMutexRelease(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    }

    return rc;
}

/**
 * Removes the given I/O request from the list of allocated I/O requests.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to insert.
 */
static int drvvdMediaExIoReqRemove(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    int rc = VINF_SUCCESS;
    unsigned idxBin = drvvdMediaExIoReqIdHash(pIoReq->uIoReqId);

    rc = RTSemFastMutexRequest(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    if (RT_SUCCESS(rc))
    {
        RTListNodeRemove(&pIoReq->NdAllocatedList);
        RTSemFastMutexRelease(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    }

    return rc;
}

/**
 * Retires a given I/O request marking it as complete and notiyfing the
 * device/driver above about the completion if requested.
 *
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to complete.
 * @param   rcReq     The status code the request completed with.
 * @param   fUpNotify Flag whether to notify the driver/device above us about the completion.
 */
static void drvvdMediaExIoReqRetire(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, int rcReq, bool fUpNotify)
{
    LogFlowFunc(("pThis=%#p pIoReq=%#p rcReq=%Rrc fUpNotify=%RTbool\n",
                 pThis, pIoReq, rcReq, fUpNotify));

    bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_COMPLETING, VDIOREQSTATE_ACTIVE);
    if (fXchg)
    {
        uint32_t cNew = ASMAtomicDecU32(&pThis->cIoReqsActive);
        AssertMsg(cNew != UINT32_MAX, ("Number of active requests underflowed!\n")); RT_NOREF(cNew);
    }
    else
    {
        Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
        rcReq = VERR_PDM_MEDIAEX_IOREQ_CANCELED;
    }

    ASMAtomicXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_COMPLETED);
    drvvdMediaExIoReqBufFree(pThis, pIoReq);

    /*
     * Leave a release log entry if the request was active for more than 25 seconds
     * (30 seconds is the timeout of the guest).
     */
    uint64_t tsNow = RTTimeMilliTS();
    if (tsNow - pIoReq->tsSubmit >= 25 * 1000)
    {
        const char *pcszReq = NULL;

        switch (pIoReq->enmType)
        {
            case PDMMEDIAEXIOREQTYPE_READ:
                pcszReq = "Read";
                break;
            case PDMMEDIAEXIOREQTYPE_WRITE:
                pcszReq = "Write";
                break;
            case PDMMEDIAEXIOREQTYPE_FLUSH:
                pcszReq = "Flush";
                break;
            case PDMMEDIAEXIOREQTYPE_DISCARD:
                pcszReq = "Discard";
                break;
            default:
                pcszReq = "<Invalid>";
        }

        LogRel(("VD#%u: %s request was active for %llu seconds\n",
                pThis->pDrvIns->iInstance, pcszReq, (tsNow - pIoReq->tsSubmit) / 1000));
    }

    if (RT_FAILURE(rcReq))
    {
        /* Log the error. */
        if (pThis->cErrors++ < DRVVD_MAX_LOG_REL_ERRORS)
        {
            if (rcReq == VERR_PDM_MEDIAEX_IOREQ_CANCELED)
            {
                if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
                    LogRel(("VD#%u: Aborted flush returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance, rcReq));
                else if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
                    LogRel(("VD#%u: Aborted discard returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance, rcReq));
                else
                    LogRel(("VD#%u: Aborted %s (%u bytes left) returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance,
                            pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
                            ? "read"
                            : "write",
                            pIoReq->ReadWrite.cbReqLeft, rcReq));
            }
            else
            {
                if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
                    LogRel(("VD#%u: Flush returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance, rcReq));
                else if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
                    LogRel(("VD#%u: Discard returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance, rcReq));
                else
                    LogRel(("VD#%u: %s (%u bytes left) returned rc=%Rrc\n",
                            pThis->pDrvIns->iInstance,
                            pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
                            ? "Read"
                            : "Write",
                            pIoReq->ReadWrite.cbReqLeft, rcReq));
            }
        }

        STAM_REL_COUNTER_INC(&pThis->StatReqsFailed);
    }
    else
    {
        STAM_REL_COUNTER_INC(&pThis->StatReqsSucceeded);

        switch (pIoReq->enmType)
        {
            case PDMMEDIAEXIOREQTYPE_READ:
                STAM_REL_COUNTER_ADD(&pThis->StatBytesRead, pIoReq->ReadWrite.cbReq);
                break;
            case PDMMEDIAEXIOREQTYPE_WRITE:
                STAM_REL_COUNTER_ADD(&pThis->StatBytesWritten, pIoReq->ReadWrite.cbReq);
                break;
            default:
                break;
        }
    }

    if (fUpNotify)
    {
        int rc = pThis->pDrvMediaExPort->pfnIoReqCompleteNotify(pThis->pDrvMediaExPort,
                                                                pIoReq, &pIoReq->abAlloc[0], rcReq);
        AssertRC(rc);
    }

    LogFlowFunc(("returns\n"));
}

/**
 * I/O request completion worker.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to complete.
 * @param   rcReq     The status code the request completed with.
 * @param   fUpNotify Flag whether to notify the driver/device above us about the completion.
 */
static int drvvdMediaExIoReqCompleteWorker(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, int rcReq, bool fUpNotify)
{
    LogFlowFunc(("pThis=%#p pIoReq=%#p rcReq=%Rrc fUpNotify=%RTbool\n",
                 pThis, pIoReq, rcReq, fUpNotify));

    /*
     * For a read we need to sync the memory before continuing to process
     * the request further.
     */
    if (   RT_SUCCESS(rcReq)
        && pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ)
        rcReq = drvvdMediaExIoReqBufSync(pThis, pIoReq, false /* fToIoBuf */);

    /*
     * When the request owner instructs us to handle recoverable errors like full disks
     * do it. Mark the request as suspended, notify the owner and put the request on the
     * redo list.
     */
    if (   RT_FAILURE(rcReq)
        && (pIoReq->fFlags & PDMIMEDIAEX_F_SUSPEND_ON_RECOVERABLE_ERR)
        && drvvdMediaExIoReqIsRedoSetWarning(pThis, rcReq))
    {
        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_SUSPENDED, VDIOREQSTATE_ACTIVE);
        if (fXchg)
        {
            /* Put on redo list and adjust active request counter. */
            RTCritSectEnter(&pThis->CritSectIoReqRedo);
            RTListAppend(&pThis->LstIoReqRedo, &pIoReq->NdLstWait);
            RTCritSectLeave(&pThis->CritSectIoReqRedo);
            uint32_t cNew = ASMAtomicDecU32(&pThis->cIoReqsActive);
            AssertMsg(cNew != UINT32_MAX, ("Number of active requests underflowed!\n")); RT_NOREF(cNew);
            pThis->pDrvMediaExPort->pfnIoReqStateChanged(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                         PDMMEDIAEXIOREQSTATE_SUSPENDED);
            LogFlowFunc(("Suspended I/O request %#p\n", pIoReq));
            rcReq = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
        }
        else
        {
            /* Request was canceled inbetween, so don't care and notify the owner about the completed request. */
            Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
            drvvdMediaExIoReqRetire(pThis, pIoReq, rcReq, fUpNotify);
        }
    }
    else
    {
        if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
            || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
        {
            /* Adjust the remaining amount to transfer. */
            Assert(pIoReq->ReadWrite.cbIoBuf > 0 || rcReq == VERR_PDM_MEDIAEX_IOREQ_CANCELED);

            size_t cbReqIo = RT_MIN(pIoReq->ReadWrite.cbReqLeft, pIoReq->ReadWrite.cbIoBuf);
            pIoReq->ReadWrite.offStart  += cbReqIo;
            pIoReq->ReadWrite.cbReqLeft -= cbReqIo;
        }

        if (   RT_FAILURE(rcReq)
            || !pIoReq->ReadWrite.cbReqLeft
            || (   pIoReq->enmType != PDMMEDIAEXIOREQTYPE_READ
                && pIoReq->enmType != PDMMEDIAEXIOREQTYPE_WRITE))
            drvvdMediaExIoReqRetire(pThis, pIoReq, rcReq, fUpNotify);
        else
            drvvdMediaExIoReqReadWriteProcess(pThis, pIoReq, fUpNotify);
    }

    LogFlowFunc(("returns %Rrc\n", rcReq));
    return rcReq;
}


/**
 * Allocates a memory buffer suitable for I/O for the given request.
 *
 * @returns VBox status code.
 * @retval  VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS if there is no I/O memory available to allocate and
 *          the request was placed on a waiting list.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to allocate memory for.
 * @param   cb        Size of the buffer.
 */
DECLINLINE(int) drvvdMediaExIoReqBufAlloc(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, size_t cb)
{
    int rc = VERR_NOT_SUPPORTED;
    LogFlowFunc(("pThis=%#p pIoReq=%#p cb=%zu\n", pThis, pIoReq, cb));

/** @todo This does not work at all with encryption enabled because the encryption plugin
 *         encrypts the data in place trashing guest memory and causing data corruption later on!
 *
 * DO NOT ENABLE UNLESS YOU WANT YOUR DATA SHREDDED!!!
 */
#if 0
    if (   cb == _4K
        && pThis->pDrvMediaExPort->pfnIoReqQueryBuf)
    {
        /* Try to get a direct pointer to the buffer first. */
        void *pvBuf = NULL;
        size_t cbBuf = 0;

        STAM_COUNTER_INC(&pThis->StatQueryBufAttempts);
        rc = pThis->pDrvMediaExPort->pfnIoReqQueryBuf(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                      &pvBuf, &cbBuf);
        if (RT_SUCCESS(rc))
        {
            STAM_COUNTER_INC(&pThis->StatQueryBufSuccess);
            pIoReq->ReadWrite.cbIoBuf           = cbBuf;
            pIoReq->ReadWrite.fDirectBuf        = true;
            pIoReq->ReadWrite.Direct.Seg.pvSeg  = pvBuf;
            pIoReq->ReadWrite.Direct.Seg.cbSeg  = cbBuf;
            RTSgBufInit(&pIoReq->ReadWrite.Direct.SgBuf, &pIoReq->ReadWrite.Direct.Seg, 1);
            pIoReq->ReadWrite.pSgBuf = &pIoReq->ReadWrite.Direct.SgBuf;
        }
    }
#endif

    if (RT_FAILURE(rc))
    {
        rc = IOBUFMgrAllocBuf(pThis->hIoBufMgr, &pIoReq->ReadWrite.IoBuf, cb, &pIoReq->ReadWrite.cbIoBuf);
        if (rc == VERR_NO_MEMORY)
        {
            LogFlowFunc(("Could not allocate memory for request, deferring\n"));
            RTCritSectEnter(&pThis->CritSectIoReqsIoBufWait);
            RTListAppend(&pThis->LstIoReqIoBufWait, &pIoReq->NdLstWait);
            ASMAtomicIncU32(&pThis->cIoReqsWaiting);
            if (ASMAtomicReadBool(&pThis->fSuspending))
                pThis->pDrvMediaExPort->pfnIoReqStateChanged(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                             PDMMEDIAEXIOREQSTATE_SUSPENDED);
            LogFlowFunc(("Suspended I/O request %#p\n", pIoReq));
            RTCritSectLeave(&pThis->CritSectIoReqsIoBufWait);
            rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
        }
        else
        {
            LogFlowFunc(("Allocated %zu bytes of memory\n", pIoReq->ReadWrite.cbIoBuf));
            Assert(pIoReq->ReadWrite.cbIoBuf > 0);
            pIoReq->ReadWrite.fDirectBuf = false;
            pIoReq->ReadWrite.pSgBuf = &pIoReq->ReadWrite.IoBuf.SgBuf;
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Wrapper around the various ways to read from the underlying medium (cache, async vs. sync).
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to process.
 * @param   cbReqIo   Transfer size.
 * @param   pcbReqIo  Where to store the amount of transferred data.
 */
static int drvvdMediaExIoReqReadWrapper(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, size_t cbReqIo, size_t *pcbReqIo)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p pIoReq=%#p cbReqIo=%zu pcbReqIo=%#p\n", pThis, pIoReq, cbReqIo, pcbReqIo));

    Assert(cbReqIo > 0);

    if (   pThis->fAsyncIOSupported
        && !(pIoReq->fFlags & PDMIMEDIAEX_F_SYNC))
    {
        if (pThis->pBlkCache)
        {
            rc = PDMDrvHlpBlkCacheRead(pThis->pDrvIns, pThis->pBlkCache, pIoReq->ReadWrite.offStart,
                                       pIoReq->ReadWrite.pSgBuf, cbReqIo, pIoReq);
            if (rc == VINF_SUCCESS)
                rc = VINF_VD_ASYNC_IO_FINISHED;
            else if (rc == VINF_AIO_TASK_PENDING)
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
        }
        else
            rc = VDAsyncRead(pThis->pDisk, pIoReq->ReadWrite.offStart, cbReqIo, pIoReq->ReadWrite.pSgBuf,
                             drvvdMediaExIoReqComplete, pThis, pIoReq);
    }
    else
    {
        void *pvBuf = RTSgBufGetNextSegment(pIoReq->ReadWrite.pSgBuf, &cbReqIo);

        Assert(cbReqIo > 0 && RT_VALID_PTR(pvBuf));
        rc = VDRead(pThis->pDisk, pIoReq->ReadWrite.offStart, pvBuf, cbReqIo);
        if (RT_SUCCESS(rc))
            rc = VINF_VD_ASYNC_IO_FINISHED;
    }

    *pcbReqIo = cbReqIo;

    LogFlowFunc(("returns %Rrc *pcbReqIo=%zu\n", rc, *pcbReqIo));
    return rc;
}

/**
 * Wrapper around the various ways to write to the underlying medium (cache, async vs. sync).
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to process.
 * @param   cbReqIo   Transfer size.
 * @param   pcbReqIo  Where to store the amount of transferred data.
 */
static int drvvdMediaExIoReqWriteWrapper(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, size_t cbReqIo, size_t *pcbReqIo)
{
    int rc = VINF_SUCCESS;

    Assert(cbReqIo > 0);

    LogFlowFunc(("pThis=%#p pIoReq=%#p cbReqIo=%zu pcbReqIo=%#p\n", pThis, pIoReq, cbReqIo, pcbReqIo));

    if (   pThis->fAsyncIOSupported
        && !(pIoReq->fFlags & PDMIMEDIAEX_F_SYNC))
    {
        if (pThis->pBlkCache)
        {
            rc = PDMDrvHlpBlkCacheWrite(pThis->pDrvIns, pThis->pBlkCache, pIoReq->ReadWrite.offStart,
                                        pIoReq->ReadWrite.pSgBuf, cbReqIo, pIoReq);
            if (rc == VINF_SUCCESS)
                rc = VINF_VD_ASYNC_IO_FINISHED;
            else if (rc == VINF_AIO_TASK_PENDING)
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
        }
        else
            rc = VDAsyncWrite(pThis->pDisk, pIoReq->ReadWrite.offStart, cbReqIo, pIoReq->ReadWrite.pSgBuf,
                              drvvdMediaExIoReqComplete, pThis, pIoReq);
    }
    else
    {
        void *pvBuf = RTSgBufGetNextSegment(pIoReq->ReadWrite.pSgBuf, &cbReqIo);

        Assert(cbReqIo > 0 && RT_VALID_PTR(pvBuf));
        rc = VDWrite(pThis->pDisk, pIoReq->ReadWrite.offStart, pvBuf, cbReqIo);
        if (RT_SUCCESS(rc))
            rc = VINF_VD_ASYNC_IO_FINISHED;

#ifdef VBOX_PERIODIC_FLUSH
        if (pThis->cbFlushInterval)
        {
            pThis->cbDataWritten += (uint32_t)cbReqIo;
            if (pThis->cbDataWritten > pThis->cbFlushInterval)
            {
                pThis->cbDataWritten = 0;
                VDFlush(pThis->pDisk);
            }
        }
#endif /* VBOX_PERIODIC_FLUSH */
    }

    *pcbReqIo = cbReqIo;

    LogFlowFunc(("returns %Rrc *pcbReqIo=%zu\n", rc, *pcbReqIo));
    return rc;
}

/**
 * Wrapper around the various ways to flush all data to the underlying medium (cache, async vs. sync).
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to process.
 */
static int drvvdMediaExIoReqFlushWrapper(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p pIoReq=%#p\n", pThis, pIoReq));

    if (   pThis->fAsyncIOSupported
        && !(pIoReq->fFlags & PDMIMEDIAEX_F_SYNC))
    {
#ifdef VBOX_IGNORE_FLUSH
        if (pThis->fIgnoreFlushAsync)
            rc = VINF_VD_ASYNC_IO_FINISHED;
        else
#endif /* VBOX_IGNORE_FLUSH */
        {
            if (pThis->pBlkCache)
            {
                rc = PDMDrvHlpBlkCacheFlush(pThis->pDrvIns, pThis->pBlkCache, pIoReq);
                if (rc == VINF_SUCCESS)
                    rc = VINF_VD_ASYNC_IO_FINISHED;
                else if (rc == VINF_AIO_TASK_PENDING)
                    rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
            }
            else
                rc = VDAsyncFlush(pThis->pDisk, drvvdMediaExIoReqComplete, pThis, pIoReq);
        }
    }
    else
    {
#ifdef VBOX_IGNORE_FLUSH
        if (pThis->fIgnoreFlush)
            rc = VINF_VD_ASYNC_IO_FINISHED;
        else
#endif /* VBOX_IGNORE_FLUSH */
        {
            rc = VDFlush(pThis->pDisk);
            if (RT_SUCCESS(rc))
                rc = VINF_VD_ASYNC_IO_FINISHED;
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Wrapper around the various ways to discard data blocks on the underlying medium (cache, async vs. sync).
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to process.
 */
static int drvvdMediaExIoReqDiscardWrapper(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p pIoReq=%#p\n", pThis, pIoReq));

    if (   pThis->fAsyncIOSupported
        && !(pIoReq->fFlags & PDMIMEDIAEX_F_SYNC))
    {
        if (pThis->pBlkCache)
        {
            rc = PDMDrvHlpBlkCacheDiscard(pThis->pDrvIns, pThis->pBlkCache,
                                          pIoReq->Discard.paRanges, pIoReq->Discard.cRanges,
                                          pIoReq);
            if (rc == VINF_SUCCESS)
                rc = VINF_VD_ASYNC_IO_FINISHED;
            else if (rc == VINF_AIO_TASK_PENDING)
                rc = VERR_VD_ASYNC_IO_IN_PROGRESS;
        }
        else
            rc = VDAsyncDiscardRanges(pThis->pDisk, pIoReq->Discard.paRanges, pIoReq->Discard.cRanges,
                                      drvvdMediaExIoReqComplete, pThis, pIoReq);
    }
    else
    {
        rc = VDDiscardRanges(pThis->pDisk, pIoReq->Discard.paRanges, pIoReq->Discard.cRanges);
        if (RT_SUCCESS(rc))
            rc = VINF_VD_ASYNC_IO_FINISHED;
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Processes a read/write request.
 *
 * @returns VBox status code.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request to process.
 * @param   fUpNotify Flag whether to notify the driver/device above us about the completion.
 */
static int drvvdMediaExIoReqReadWriteProcess(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq, bool fUpNotify)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p pIoReq=%#p fUpNotify=%RTbool\n", pThis, pIoReq, fUpNotify));

    Assert(pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE);

    rc = drvvdKeyCheckPrereqs(pThis, false /* fSetError */);

    while (   pIoReq->ReadWrite.cbReqLeft
           && rc == VINF_SUCCESS)
    {
        Assert(pIoReq->ReadWrite.cbIoBuf > 0);

        size_t cbReqIo = RT_MIN(pIoReq->ReadWrite.cbReqLeft, pIoReq->ReadWrite.cbIoBuf);

        if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ)
            rc = drvvdMediaExIoReqReadWrapper(pThis, pIoReq, cbReqIo, &cbReqIo);
        else
        {
            /* Sync memory buffer from the request initiator. */
            rc = drvvdMediaExIoReqBufSync(pThis, pIoReq, true /* fToIoBuf */);
            if (RT_SUCCESS(rc))
                rc = drvvdMediaExIoReqWriteWrapper(pThis, pIoReq, cbReqIo, &cbReqIo);
        }

        if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
        else if (rc == VINF_VD_ASYNC_IO_FINISHED)
        {
            /*
             * Don't sync the buffer or update the I/O state for the last chunk as it is done
             * already in the completion worker called below.
             */
            if (cbReqIo < pIoReq->ReadWrite.cbReqLeft)
            {
                if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ)
                    rc = drvvdMediaExIoReqBufSync(pThis, pIoReq, false /* fToIoBuf */);
                else
                    rc = VINF_SUCCESS;
                pIoReq->ReadWrite.offStart  += cbReqIo;
                pIoReq->ReadWrite.cbReqLeft -= cbReqIo;
            }
            else
            {
                rc = VINF_SUCCESS;
                break;
            }
        }
    }

    if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
        rc = drvvdMediaExIoReqCompleteWorker(pThis, pIoReq, rc, fUpNotify);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Tries to process any requests waiting for available I/O memory.
 *
 * @param   pThis     VBox disk container instance data.
 */
static void drvvdMediaExIoReqProcessWaiting(PVBOXDISK pThis)
{
    uint32_t cIoReqsWaiting = ASMAtomicXchgU32(&pThis->cIoReqsWaiting, 0);
    if (cIoReqsWaiting > 0)
    {
        RTLISTANCHOR LstIoReqProcess;
        RTLISTANCHOR LstIoReqCanceled;
        RTListInit(&LstIoReqProcess);
        RTListInit(&LstIoReqCanceled);

        /* Try to process as many requests as possible. */
        RTCritSectEnter(&pThis->CritSectIoReqsIoBufWait);
        PPDMMEDIAEXIOREQINT pIoReqCur, pIoReqNext;

        RTListForEachSafe(&pThis->LstIoReqIoBufWait, pIoReqCur, pIoReqNext, PDMMEDIAEXIOREQINT, NdLstWait)
        {
            LogFlowFunc(("Found I/O request %#p on waiting list, trying to allocate buffer of size %zu bytes\n",
                         pIoReqCur, pIoReqCur->ReadWrite.cbReq));

            /* Allocate a suitable I/O buffer for this request. */
            int rc = IOBUFMgrAllocBuf(pThis->hIoBufMgr, &pIoReqCur->ReadWrite.IoBuf, pIoReqCur->ReadWrite.cbReq,
                                      &pIoReqCur->ReadWrite.cbIoBuf);
            if (rc == VINF_SUCCESS)
            {
                Assert(pIoReqCur->ReadWrite.cbIoBuf > 0);

                cIoReqsWaiting--;
                RTListNodeRemove(&pIoReqCur->NdLstWait);

                pIoReqCur->ReadWrite.fDirectBuf = false;
                pIoReqCur->ReadWrite.pSgBuf     = &pIoReqCur->ReadWrite.IoBuf.SgBuf;

                bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReqCur->enmState,
                                                 VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
                if (RT_UNLIKELY(!fXchg))
                {
                    /* Must have been canceled inbetween. */
                    Assert(pIoReqCur->enmState == VDIOREQSTATE_CANCELED);

                    /* Free the buffer here already again to let other requests get a chance to allocate the memory. */
                    IOBUFMgrFreeBuf(&pIoReqCur->ReadWrite.IoBuf);
                    pIoReqCur->ReadWrite.cbIoBuf = 0;
                    RTListAppend(&LstIoReqCanceled, &pIoReqCur->NdLstWait);
                }
                else
                {
                    ASMAtomicIncU32(&pThis->cIoReqsActive);
                    RTListAppend(&LstIoReqProcess, &pIoReqCur->NdLstWait);
                }
            }
            else
            {
                Assert(rc == VERR_NO_MEMORY);
                break;
            }
        }
        RTCritSectLeave(&pThis->CritSectIoReqsIoBufWait);

        ASMAtomicAddU32(&pThis->cIoReqsWaiting, cIoReqsWaiting);

        /* Process the requests we could allocate memory for and the ones which got canceled outside the lock now. */
        RTListForEachSafe(&LstIoReqCanceled, pIoReqCur, pIoReqNext, PDMMEDIAEXIOREQINT, NdLstWait)
        {
            RTListNodeRemove(&pIoReqCur->NdLstWait);
            drvvdMediaExIoReqCompleteWorker(pThis, pIoReqCur, VERR_PDM_MEDIAEX_IOREQ_CANCELED, true /* fUpNotify */);
        }

        RTListForEachSafe(&LstIoReqProcess, pIoReqCur, pIoReqNext, PDMMEDIAEXIOREQINT, NdLstWait)
        {
            RTListNodeRemove(&pIoReqCur->NdLstWait);
            drvvdMediaExIoReqReadWriteProcess(pThis, pIoReqCur, true /* fUpNotify */);
        }
    }
}

/**
 * Frees a I/O memory buffer allocated previously.
 *
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    I/O request for which to free memory.
 */
DECLINLINE(void) drvvdMediaExIoReqBufFree(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    LogFlowFunc(("pThis=%#p pIoReq=%#p{.cbIoBuf=%zu}\n", pThis, pIoReq, pIoReq->ReadWrite.cbIoBuf));

    if (   (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
            || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
        && !pIoReq->ReadWrite.fDirectBuf
        && pIoReq->ReadWrite.cbIoBuf > 0)
    {
        IOBUFMgrFreeBuf(&pIoReq->ReadWrite.IoBuf);

        if (!ASMAtomicReadBool(&pThis->fSuspending))
            drvvdMediaExIoReqProcessWaiting(pThis);
    }

    LogFlowFunc(("returns\n"));
}


/**
 * Returns a string description of the given request state.
 *
 * @returns Pointer to the stringified state.
 * @param   enmState  The state.
 */
DECLINLINE(const char *) drvvdMediaExIoReqStateStringify(VDIOREQSTATE enmState)
{
#define STATE2STR(a_State) case VDIOREQSTATE_##a_State: return #a_State
    switch (enmState)
    {
        STATE2STR(INVALID);
        STATE2STR(FREE);
        STATE2STR(ALLOCATED);
        STATE2STR(ACTIVE);
        STATE2STR(SUSPENDED);
        STATE2STR(COMPLETING);
        STATE2STR(COMPLETED);
        STATE2STR(CANCELED);
        default:
            AssertMsgFailed(("Unknown state %u\n", enmState));
            return "UNKNOWN";
    }
#undef STATE2STR
}


/**
 * Returns a string description of the given request type.
 *
 * @returns Pointer to the stringified type.
 * @param   enmType  The request type.
 */
DECLINLINE(const char *) drvvdMediaExIoReqTypeStringify(PDMMEDIAEXIOREQTYPE enmType)
{
#define TYPE2STR(a_Type) case PDMMEDIAEXIOREQTYPE_##a_Type: return #a_Type
    switch (enmType)
    {
        TYPE2STR(INVALID);
        TYPE2STR(FLUSH);
        TYPE2STR(WRITE);
        TYPE2STR(READ);
        TYPE2STR(DISCARD);
        TYPE2STR(SCSI);
        default:
            AssertMsgFailed(("Unknown type %u\n", enmType));
            return "UNKNOWN";
    }
#undef TYPE2STR
}


/**
 * Dumps the interesting bits about the given I/O request to the release log.
 *
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    The I/O request to dump.
 */
static void drvvdMediaExIoReqLogRel(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    uint64_t offStart = 0;
    size_t cbReq = 0;
    size_t cbLeft = 0;
    size_t cbBufSize = 0;
    uint64_t tsActive = RTTimeMilliTS() - pIoReq->tsSubmit;

    if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
        || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
    {
        offStart = pIoReq->ReadWrite.offStart;
        cbReq = pIoReq->ReadWrite.cbReq;
        cbLeft = pIoReq->ReadWrite.cbReqLeft;
        cbBufSize = pIoReq->ReadWrite.cbIoBuf;
    }

    LogRel(("VD#%u: Request{%#p}:\n"
            "    Type=%s State=%s Id=%#llx SubmitTs=%llu {%llu} Flags=%#x\n"
            "    Offset=%llu Size=%zu Left=%zu BufSize=%zu\n",
            pThis->pDrvIns->iInstance, pIoReq,
            drvvdMediaExIoReqTypeStringify(pIoReq->enmType),
            drvvdMediaExIoReqStateStringify(pIoReq->enmState),
            pIoReq->uIoReqId, pIoReq->tsSubmit, tsActive, pIoReq->fFlags,
            offStart, cbReq, cbLeft, cbBufSize));
}


/**
 * Returns whether the VM is in a running state.
 *
 * @returns Flag indicating whether the VM is currently in a running state.
 * @param   pThis     VBox disk container instance data.
 */
DECLINLINE(bool) drvvdMediaExIoReqIsVmRunning(PVBOXDISK pThis)
{
    VMSTATE enmVmState = PDMDrvHlpVMState(pThis->pDrvIns);
    if (   enmVmState == VMSTATE_RESUMING
        || enmVmState == VMSTATE_RUNNING
        || enmVmState == VMSTATE_RUNNING_LS
        || enmVmState == VMSTATE_RESETTING
        || enmVmState == VMSTATE_RESETTING_LS
        || enmVmState == VMSTATE_SOFT_RESETTING
        || enmVmState == VMSTATE_SOFT_RESETTING_LS
        || enmVmState == VMSTATE_SUSPENDING
        || enmVmState == VMSTATE_SUSPENDING_LS
        || enmVmState == VMSTATE_SUSPENDING_EXT_LS)
        return true;

    return false;
}

/**
 * @copydoc FNVDASYNCTRANSFERCOMPLETE
 */
static DECLCALLBACK(void) drvvdMediaExIoReqComplete(void *pvUser1, void *pvUser2, int rcReq)
{
    PVBOXDISK pThis = (PVBOXDISK)pvUser1;
    PPDMMEDIAEXIOREQINT pIoReq = (PPDMMEDIAEXIOREQINT)pvUser2;

    drvvdMediaExIoReqCompleteWorker(pThis, pIoReq, rcReq, true /* fUpNotify */);
}

/**
 * Tries to cancel the given I/O request returning the result.
 *
 * @returns Flag whether the request was successfully canceled or whether it
 *          already complete inbetween.
 * @param   pThis     VBox disk container instance data.
 * @param   pIoReq    The I/O request to cancel.
 */
static bool drvvdMediaExIoReqCancel(PVBOXDISK pThis, PPDMMEDIAEXIOREQINT pIoReq)
{
    bool fXchg = false;
    VDIOREQSTATE enmStateOld = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    drvvdMediaExIoReqLogRel(pThis, pIoReq);

    /*
     * We might have to try canceling the request multiple times if it transitioned from
     * ALLOCATED to ACTIVE or to SUSPENDED between reading the state and trying to change it.
     */
    while (   (   enmStateOld == VDIOREQSTATE_ALLOCATED
               || enmStateOld == VDIOREQSTATE_ACTIVE
               || enmStateOld == VDIOREQSTATE_SUSPENDED)
           && !fXchg)
    {
        fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_CANCELED, enmStateOld);
        if (fXchg)
            break;

        enmStateOld = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);
    }

    if (fXchg && enmStateOld == VDIOREQSTATE_ACTIVE)
    {
        uint32_t cNew = ASMAtomicDecU32(&pThis->cIoReqsActive);
        AssertMsg(cNew != UINT32_MAX, ("Number of active requests underflowed!\n")); RT_NOREF(cNew);
    }

    return fXchg;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnQueryFeatures}
 */
static DECLCALLBACK(int) drvvdQueryFeatures(PPDMIMEDIAEX pInterface, uint32_t *pfFeatures)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);

    AssertPtrReturn(pfFeatures, VERR_INVALID_POINTER);

    uint32_t fFeatures = 0;
    if (pThis->fAsyncIOSupported)
        fFeatures |= PDMIMEDIAEX_FEATURE_F_ASYNC;
    if (pThis->IMedia.pfnDiscard)
        fFeatures |= PDMIMEDIAEX_FEATURE_F_DISCARD;

    *pfFeatures = fFeatures;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIMEDIAEX,pfnNotifySuspend}
 */
static DECLCALLBACK(void) drvvdNotifySuspend(PPDMIMEDIAEX pInterface)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);

    ASMAtomicXchgBool(&pThis->fSuspending, true);

    /* Mark all waiting requests as suspended so they don't get accounted for. */
    RTCritSectEnter(&pThis->CritSectIoReqsIoBufWait);
    PPDMMEDIAEXIOREQINT pIoReqCur, pIoReqNext;
    RTListForEachSafe(&pThis->LstIoReqIoBufWait, pIoReqCur, pIoReqNext, PDMMEDIAEXIOREQINT, NdLstWait)
    {
        pThis->pDrvMediaExPort->pfnIoReqStateChanged(pThis->pDrvMediaExPort, pIoReqCur, &pIoReqCur->abAlloc[0],
                                                     PDMMEDIAEXIOREQSTATE_SUSPENDED);
        LogFlowFunc(("Suspended I/O request %#p\n", pIoReqCur));
    }
    RTCritSectLeave(&pThis->CritSectIoReqsIoBufWait);
}


/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqAllocSizeSet}
 */
static DECLCALLBACK(int) drvvdIoReqAllocSizeSet(PPDMIMEDIAEX pInterface, size_t cbIoReqAlloc)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    if (RT_UNLIKELY(pThis->hIoReqCache != NIL_RTMEMCACHE))
        return VERR_INVALID_STATE;

    return RTMemCacheCreate(&pThis->hIoReqCache, sizeof(PDMMEDIAEXIOREQINT) + cbIoReqAlloc, 0, UINT32_MAX,
                            NULL, NULL, NULL, 0);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqAlloc}
 */
static DECLCALLBACK(int) drvvdIoReqAlloc(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq, void **ppvIoReqAlloc,
                                         PDMMEDIAEXIOREQID uIoReqId, uint32_t fFlags)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);

    AssertReturn(!(fFlags & ~PDMIMEDIAEX_F_VALID), VERR_INVALID_PARAMETER);

    PPDMMEDIAEXIOREQINT pIoReq = (PPDMMEDIAEXIOREQINT)RTMemCacheAlloc(pThis->hIoReqCache);

    if (RT_UNLIKELY(!pIoReq))
        return VERR_NO_MEMORY;

    pIoReq->uIoReqId      = uIoReqId;
    pIoReq->fFlags        = fFlags;
    pIoReq->pDisk         = pThis;
    pIoReq->enmState      = VDIOREQSTATE_ALLOCATED;
    pIoReq->enmType       = PDMMEDIAEXIOREQTYPE_INVALID;

    int rc = drvvdMediaExIoReqInsert(pThis, pIoReq);
    if (RT_SUCCESS(rc))
    {
        *phIoReq = pIoReq;
        *ppvIoReqAlloc = &pIoReq->abAlloc[0];
    }
    else
        RTMemCacheFree(pThis->hIoReqCache, pIoReq);

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqFree}
 */
static DECLCALLBACK(int) drvvdIoReqFree(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    if (   pIoReq->enmState != VDIOREQSTATE_COMPLETED
        && pIoReq->enmState != VDIOREQSTATE_ALLOCATED)
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    /* Remove from allocated list. */
    int rc = drvvdMediaExIoReqRemove(pThis, pIoReq);
    if (RT_FAILURE(rc))
        return rc;

    /* Free any associated I/O memory. */
    drvvdMediaExIoReqBufFree(pThis, pIoReq);

    /* For discard request discard the range array. */
    if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD
        && pIoReq->Discard.paRanges)
    {
        RTMemFree(pIoReq->Discard.paRanges);
        pIoReq->Discard.paRanges = NULL;
    }

    pIoReq->enmState = VDIOREQSTATE_FREE;
    RTMemCacheFree(pThis->hIoReqCache, pIoReq);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryResidual}
 */
static DECLCALLBACK(int) drvvdIoReqQueryResidual(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbResidual)
{
    RT_NOREF1(pInterface);

    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    if (pIoReq->enmState != VDIOREQSTATE_COMPLETED)
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    if (   pIoReq->enmType != PDMMEDIAEXIOREQTYPE_READ
        && pIoReq->enmType != PDMMEDIAEXIOREQTYPE_WRITE
        && pIoReq->enmType != PDMMEDIAEXIOREQTYPE_FLUSH)
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    *pcbResidual = 0; /* No data left to transfer always. */
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQueryXferSize}
 */
static DECLCALLBACK(int) drvvdIoReqQueryXferSize(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, size_t *pcbXfer)
{
    int rc = VINF_SUCCESS;
    RT_NOREF1(pInterface);

    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    if (pIoReq->enmState != VDIOREQSTATE_COMPLETED)
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
        || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
        *pcbXfer = pIoReq->ReadWrite.cbReq;
    else if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
        *pcbXfer = 0;
    else
        rc = VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancelAll}
 */
static DECLCALLBACK(int) drvvdIoReqCancelAll(PPDMIMEDIAEX pInterface)
{
    int rc = VINF_SUCCESS;
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);

    LogRel(("VD#%u: Cancelling all active requests\n", pThis->pDrvIns->iInstance));

    for (unsigned idxBin = 0; idxBin < RT_ELEMENTS(pThis->aIoReqAllocBins); idxBin++)
    {
        rc = RTSemFastMutexRequest(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
        if (RT_SUCCESS(rc))
        {
            /* Search for I/O request with ID. */
            PPDMMEDIAEXIOREQINT pIt;

            RTListForEach(&pThis->aIoReqAllocBins[idxBin].LstIoReqAlloc, pIt, PDMMEDIAEXIOREQINT, NdAllocatedList)
            {
                drvvdMediaExIoReqCancel(pThis, pIt);
            }
            RTSemFastMutexRelease(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
        }
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqCancel}
 */
static DECLCALLBACK(int) drvvdIoReqCancel(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQID uIoReqId)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    unsigned idxBin = drvvdMediaExIoReqIdHash(uIoReqId);

    LogRel(("VD#%u: Trying to cancel request %#llx\n", pThis->pDrvIns->iInstance, uIoReqId));

    int rc = RTSemFastMutexRequest(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    if (RT_SUCCESS(rc))
    {
        /* Search for I/O request with ID. */
        PPDMMEDIAEXIOREQINT pIt;
        rc = VERR_PDM_MEDIAEX_IOREQID_NOT_FOUND;

        RTListForEach(&pThis->aIoReqAllocBins[idxBin].LstIoReqAlloc, pIt, PDMMEDIAEXIOREQINT, NdAllocatedList)
        {
            if (pIt->uIoReqId == uIoReqId)
            {
                if (drvvdMediaExIoReqCancel(pThis, pIt))
                    rc = VINF_SUCCESS;

                break;
            }
        }
        RTSemFastMutexRelease(pThis->aIoReqAllocBins[idxBin].hMtxLstIoReqAlloc);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqRead}
 */
static DECLCALLBACK(int) drvvdIoReqRead(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbRead)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsRead);

    pIoReq->enmType             = PDMMEDIAEXIOREQTYPE_READ;
    pIoReq->tsSubmit            = RTTimeMilliTS();
    pIoReq->ReadWrite.offStart  = off;
    pIoReq->ReadWrite.cbReq     = cbRead;
    pIoReq->ReadWrite.cbReqLeft = cbRead;
    /* Allocate a suitable I/O buffer for this request. */
    int rc = drvvdMediaExIoReqBufAlloc(pThis, pIoReq, cbRead);
    if (rc == VINF_SUCCESS)
    {
        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
        if (RT_UNLIKELY(!fXchg))
        {
            /* Must have been canceled inbetween. */
            Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
            return VERR_PDM_MEDIAEX_IOREQ_CANCELED;
        }
        ASMAtomicIncU32(&pThis->cIoReqsActive);

        rc = drvvdMediaExIoReqReadWriteProcess(pThis, pIoReq, false /* fUpNotify */);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqWrite}
 */
static DECLCALLBACK(int) drvvdIoReqWrite(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, uint64_t off, size_t cbWrite)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsWrite);

    pIoReq->enmType             = PDMMEDIAEXIOREQTYPE_WRITE;
    pIoReq->tsSubmit            = RTTimeMilliTS();
    pIoReq->ReadWrite.offStart  = off;
    pIoReq->ReadWrite.cbReq     = cbWrite;
    pIoReq->ReadWrite.cbReqLeft = cbWrite;
    /* Allocate a suitable I/O buffer for this request. */
    int rc = drvvdMediaExIoReqBufAlloc(pThis, pIoReq, cbWrite);
    if (rc == VINF_SUCCESS)
    {
        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
        if (RT_UNLIKELY(!fXchg))
        {
            /* Must have been canceled inbetween. */
            Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
            return VERR_PDM_MEDIAEX_IOREQ_CANCELED;
        }
        ASMAtomicIncU32(&pThis->cIoReqsActive);

        rc = drvvdMediaExIoReqReadWriteProcess(pThis, pIoReq, false /* fUpNotify */);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqFlush}
 */
static DECLCALLBACK(int) drvvdIoReqFlush(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsFlush);

    pIoReq->enmType  = PDMMEDIAEXIOREQTYPE_FLUSH;
    pIoReq->tsSubmit = RTTimeMilliTS();
    bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
    if (RT_UNLIKELY(!fXchg))
    {
        /* Must have been canceled inbetween. */
        Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;
    }

    ASMAtomicIncU32(&pThis->cIoReqsActive);
    int rc = drvvdMediaExIoReqFlushWrapper(pThis, pIoReq);
    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
        rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
    else if (rc == VINF_VD_ASYNC_IO_FINISHED)
        rc = VINF_SUCCESS;

    if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
        rc = drvvdMediaExIoReqCompleteWorker(pThis, pIoReq, rc, false /* fUpNotify */);

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqDiscard}
 */
static DECLCALLBACK(int) drvvdIoReqDiscard(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq, unsigned cRangesMax)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    STAM_REL_COUNTER_INC(&pThis->StatReqsSubmitted);
    STAM_REL_COUNTER_INC(&pThis->StatReqsDiscard);

    /* Copy the ranges over now, this can be optimized in the future. */
    pIoReq->Discard.paRanges = (PRTRANGE)RTMemAllocZ(cRangesMax * sizeof(RTRANGE));
    if (RT_UNLIKELY(!pIoReq->Discard.paRanges))
        return VERR_NO_MEMORY;

    int rc = pThis->pDrvMediaExPort->pfnIoReqQueryDiscardRanges(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                                0, cRangesMax, pIoReq->Discard.paRanges,
                                                                &pIoReq->Discard.cRanges);
    if (RT_SUCCESS(rc))
    {
        pIoReq->enmType  = PDMMEDIAEXIOREQTYPE_DISCARD;
        pIoReq->tsSubmit = RTTimeMilliTS();
        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_ALLOCATED);
        if (RT_UNLIKELY(!fXchg))
        {
            /* Must have been canceled inbetween. */
            Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
            return VERR_PDM_MEDIAEX_IOREQ_CANCELED;
        }

        ASMAtomicIncU32(&pThis->cIoReqsActive);
        rc = drvvdMediaExIoReqDiscardWrapper(pThis, pIoReq);
        if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
            rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
        else if (rc == VINF_VD_ASYNC_IO_FINISHED)
            rc = VINF_SUCCESS;

        if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
            rc = drvvdMediaExIoReqCompleteWorker(pThis, pIoReq, rc, false /* fUpNotify */);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqSendScsiCmd}
 */
static DECLCALLBACK(int) drvvdIoReqSendScsiCmd(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq,
                                               uint32_t uLun, const uint8_t *pbCdb, size_t cbCdb,
                                               PDMMEDIAEXIOREQSCSITXDIR enmTxDir, PDMMEDIAEXIOREQSCSITXDIR *penmTxDirRet,
                                               size_t cbBuf, uint8_t *pabSense, size_t cbSense, size_t *pcbSenseRet,
                                               uint8_t *pu8ScsiSts, uint32_t cTimeoutMillies)
{
    RT_NOREF12(pInterface, uLun, pbCdb, cbCdb, enmTxDir, penmTxDirRet, cbBuf, pabSense, cbSense, pcbSenseRet, pu8ScsiSts, cTimeoutMillies);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;
    VDIOREQSTATE enmState = (VDIOREQSTATE)ASMAtomicReadU32((volatile uint32_t *)&pIoReq->enmState);

    if (RT_UNLIKELY(enmState == VDIOREQSTATE_CANCELED))
        return VERR_PDM_MEDIAEX_IOREQ_CANCELED;

    if (RT_UNLIKELY(enmState != VDIOREQSTATE_ALLOCATED))
        return VERR_PDM_MEDIAEX_IOREQ_INVALID_STATE;

    return VERR_NOT_SUPPORTED;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetActiveCount}
 */
static DECLCALLBACK(uint32_t) drvvdIoReqGetActiveCount(PPDMIMEDIAEX pInterface)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    return ASMAtomicReadU32(&pThis->cIoReqsActive);
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqGetSuspendedCount}
 */
static DECLCALLBACK(uint32_t) drvvdIoReqGetSuspendedCount(PPDMIMEDIAEX pInterface)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);

    AssertReturn(!drvvdMediaExIoReqIsVmRunning(pThis), 0);

    uint32_t cIoReqSuspended = 0;
    PPDMMEDIAEXIOREQINT pIoReq;
    RTCritSectEnter(&pThis->CritSectIoReqRedo);
    RTListForEach(&pThis->LstIoReqRedo, pIoReq, PDMMEDIAEXIOREQINT, NdLstWait)
    {
        cIoReqSuspended++;
    }
    RTCritSectLeave(&pThis->CritSectIoReqRedo);

    return cIoReqSuspended + pThis->cIoReqsWaiting;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedStart}
 */
static DECLCALLBACK(int) drvvdIoReqQuerySuspendedStart(PPDMIMEDIAEX pInterface, PPDMMEDIAEXIOREQ phIoReq,
                                                       void **ppvIoReqAlloc)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);

    AssertReturn(!drvvdMediaExIoReqIsVmRunning(pThis), VERR_INVALID_STATE);
    AssertReturn(!(   RTListIsEmpty(&pThis->LstIoReqRedo)
                   && RTListIsEmpty(&pThis->LstIoReqIoBufWait)), VERR_NOT_FOUND);

    PRTLISTANCHOR pLst;
    PRTCRITSECT pCritSect;
    if (!RTListIsEmpty(&pThis->LstIoReqRedo))
    {
        pLst = &pThis->LstIoReqRedo;
        pCritSect = &pThis->CritSectIoReqRedo;
    }
    else
    {
        pLst = &pThis->LstIoReqIoBufWait;
        pCritSect = &pThis->CritSectIoReqsIoBufWait;
    }

    RTCritSectEnter(pCritSect);
    PPDMMEDIAEXIOREQINT pIoReq = RTListGetFirst(pLst, PDMMEDIAEXIOREQINT, NdLstWait);
    *phIoReq       = pIoReq;
    *ppvIoReqAlloc = &pIoReq->abAlloc[0];
    RTCritSectLeave(pCritSect);

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqQuerySuspendedNext}
 */
static DECLCALLBACK(int) drvvdIoReqQuerySuspendedNext(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                      PPDMMEDIAEXIOREQ phIoReqNext, void **ppvIoReqAllocNext)
{
    PVBOXDISK pThis = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    AssertReturn(!drvvdMediaExIoReqIsVmRunning(pThis), VERR_INVALID_STATE);
    AssertPtrReturn(pIoReq, VERR_INVALID_HANDLE);
    AssertReturn(   (   pIoReq->enmState == VDIOREQSTATE_SUSPENDED
                     && (   !RTListNodeIsLast(&pThis->LstIoReqRedo, &pIoReq->NdLstWait)
                         || !RTListIsEmpty(&pThis->LstIoReqIoBufWait)))
                 || (   pIoReq->enmState == VDIOREQSTATE_ALLOCATED
                     && !RTListNodeIsLast(&pThis->LstIoReqIoBufWait, &pIoReq->NdLstWait)), VERR_NOT_FOUND);

    PPDMMEDIAEXIOREQINT pIoReqNext;
    if (pIoReq->enmState == VDIOREQSTATE_SUSPENDED)
    {
        if (!RTListNodeIsLast(&pThis->LstIoReqRedo, &pIoReq->NdLstWait))
        {
            RTCritSectEnter(&pThis->CritSectIoReqRedo);
            pIoReqNext = RTListNodeGetNext(&pIoReq->NdLstWait, PDMMEDIAEXIOREQINT, NdLstWait);
            RTCritSectLeave(&pThis->CritSectIoReqRedo);
        }
        else
        {
            RTCritSectEnter(&pThis->CritSectIoReqsIoBufWait);
            pIoReqNext = RTListGetFirst(&pThis->LstIoReqIoBufWait, PDMMEDIAEXIOREQINT, NdLstWait);
            RTCritSectLeave(&pThis->CritSectIoReqsIoBufWait);
        }
    }
    else
    {
        RTCritSectEnter(&pThis->CritSectIoReqsIoBufWait);
        pIoReqNext = RTListNodeGetNext(&pIoReq->NdLstWait, PDMMEDIAEXIOREQINT, NdLstWait);
        RTCritSectLeave(&pThis->CritSectIoReqsIoBufWait);
    }

    *phIoReqNext       = pIoReqNext;
    *ppvIoReqAllocNext = &pIoReqNext->abAlloc[0];

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedSave}
 */
static DECLCALLBACK(int) drvvdIoReqSuspendedSave(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    PVBOXDISK           pThis  = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    PCPDMDRVHLPR3       pHlp   = pThis->pDrvIns->pHlpR3;
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    AssertReturn(!drvvdMediaExIoReqIsVmRunning(pThis), VERR_INVALID_STATE);
    AssertPtrReturn(pIoReq, VERR_INVALID_HANDLE);
    AssertReturn(   pIoReq->enmState == VDIOREQSTATE_SUSPENDED
                 || pIoReq->enmState == VDIOREQSTATE_ALLOCATED, VERR_INVALID_STATE);

    pHlp->pfnSSMPutU32(pSSM, DRVVD_IOREQ_SAVED_STATE_VERSION);
    pHlp->pfnSSMPutU32(pSSM, (uint32_t)pIoReq->enmType);
    pHlp->pfnSSMPutU32(pSSM, pIoReq->uIoReqId);
    pHlp->pfnSSMPutU32(pSSM, pIoReq->fFlags);
    if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
        || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
    {
        pHlp->pfnSSMPutU64(pSSM, pIoReq->ReadWrite.offStart);
        pHlp->pfnSSMPutU64(pSSM, pIoReq->ReadWrite.cbReq);
        pHlp->pfnSSMPutU64(pSSM, pIoReq->ReadWrite.cbReqLeft);
    }
    else if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
    {
        pHlp->pfnSSMPutU32(pSSM, pIoReq->Discard.cRanges);
        for (unsigned i = 0; i < pIoReq->Discard.cRanges; i++)
        {
            pHlp->pfnSSMPutU64(pSSM, pIoReq->Discard.paRanges[i].offStart);
            pHlp->pfnSSMPutU64(pSSM, pIoReq->Discard.paRanges[i].cbRange);
        }
    }

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}

/**
 * @interface_method_impl{PDMIMEDIAEX,pfnIoReqSuspendedLoad}
 */
static DECLCALLBACK(int) drvvdIoReqSuspendedLoad(PPDMIMEDIAEX pInterface, PSSMHANDLE pSSM, PDMMEDIAEXIOREQ hIoReq)
{
    PVBOXDISK           pThis  = RT_FROM_MEMBER(pInterface, VBOXDISK, IMediaEx);
    PCPDMDRVHLPR3       pHlp   = pThis->pDrvIns->pHlpR3;
    PPDMMEDIAEXIOREQINT pIoReq = hIoReq;

    AssertReturn(!drvvdMediaExIoReqIsVmRunning(pThis), VERR_INVALID_STATE);
    AssertPtrReturn(pIoReq, VERR_INVALID_HANDLE);
    AssertReturn(pIoReq->enmState == VDIOREQSTATE_ALLOCATED, VERR_INVALID_STATE);

    uint32_t u32;
    uint64_t u64;
    int rc = VINF_SUCCESS;
    bool fPlaceOnRedoList = true;

    pHlp->pfnSSMGetU32(pSSM, &u32);
    if (u32 <= DRVVD_IOREQ_SAVED_STATE_VERSION)
    {
        pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertReturn(   u32 == PDMMEDIAEXIOREQTYPE_WRITE
                     || u32 == PDMMEDIAEXIOREQTYPE_READ
                     || u32 == PDMMEDIAEXIOREQTYPE_DISCARD
                     || u32 == PDMMEDIAEXIOREQTYPE_FLUSH,
                     VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
        pIoReq->enmType = (PDMMEDIAEXIOREQTYPE)u32;

        pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertReturn(u32 == pIoReq->uIoReqId, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

        pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertReturn(u32 == pIoReq->fFlags, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

        if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
            || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
        {
            pHlp->pfnSSMGetU64(pSSM, &pIoReq->ReadWrite.offStart);
            pHlp->pfnSSMGetU64(pSSM, &u64);
            pIoReq->ReadWrite.cbReq = (size_t)u64;
            pHlp->pfnSSMGetU64(pSSM, &u64);
            pIoReq->ReadWrite.cbReqLeft = (size_t)u64;

            /*
             * Try to allocate enough I/O buffer, if this fails for some reason put it onto the
             * waiting list instead of the redo list.
             */
            pIoReq->ReadWrite.cbIoBuf = 0;
            rc = IOBUFMgrAllocBuf(pThis->hIoBufMgr, &pIoReq->ReadWrite.IoBuf, pIoReq->ReadWrite.cbReqLeft,
                                  &pIoReq->ReadWrite.cbIoBuf);
            if (rc == VERR_NO_MEMORY)
            {
                pIoReq->enmState = VDIOREQSTATE_ALLOCATED;
                ASMAtomicIncU32(&pThis->cIoReqsWaiting);
                RTListAppend(&pThis->LstIoReqIoBufWait, &pIoReq->NdLstWait);
                fPlaceOnRedoList = false;
                rc = VINF_SUCCESS;
            }
            else
            {
                pIoReq->ReadWrite.fDirectBuf = false;
                pIoReq->ReadWrite.pSgBuf     = &pIoReq->ReadWrite.IoBuf.SgBuf;
            }
        }
        else if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
        {
            rc = pHlp->pfnSSMGetU32(pSSM, &pIoReq->Discard.cRanges);
            if (RT_SUCCESS(rc))
            {
                pIoReq->Discard.paRanges = (PRTRANGE)RTMemAllocZ(pIoReq->Discard.cRanges * sizeof(RTRANGE));
                if (RT_LIKELY(pIoReq->Discard.paRanges))
                {
                    for (unsigned i = 0; i < pIoReq->Discard.cRanges; i++)
                    {
                        pHlp->pfnSSMGetU64(pSSM, &pIoReq->Discard.paRanges[i].offStart);
                        pHlp->pfnSSMGetU64(pSSM, &u64);
                        pIoReq->Discard.paRanges[i].cbRange = (size_t)u64;
                    }
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }

        if (RT_SUCCESS(rc))
            rc = pHlp->pfnSSMGetU32(pSSM, &u32); /* sanity/terminator */
        if (RT_SUCCESS(rc))
            AssertReturn(u32 == UINT32_MAX, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
        if (   RT_SUCCESS(rc)
            && fPlaceOnRedoList)
        {
            /* Mark as suspended */
            pIoReq->enmState = VDIOREQSTATE_SUSPENDED;

            /* Link into suspended list so it gets kicked off again when we resume. */
            RTCritSectEnter(&pThis->CritSectIoReqRedo);
            RTListAppend(&pThis->LstIoReqRedo, &pIoReq->NdLstWait);
            RTCritSectLeave(&pThis->CritSectIoReqRedo);
        }
    }

    return rc;
}

/**
 * Loads all configured plugins.
 *
 * @returns VBox status code.
 * @param   pDrvIns  Driver instance data.
 * @param   pCfg     CFGM node holding plugin list.
 */
static int drvvdLoadPlugins(PPDMDRVINS pDrvIns, PCFGMNODE pCfg)
{
    PCPDMDRVHLPR3 pHlp = pDrvIns->pHlpR3;

    PCFGMNODE pCfgPlugins = pHlp->pfnCFGMGetChild(pCfg, "Plugins");

    if (pCfgPlugins)
    {
        PCFGMNODE pPluginCur = pHlp->pfnCFGMGetFirstChild(pCfgPlugins);
        while (pPluginCur)
        {
            int rc = VINF_SUCCESS;
            char *pszPluginFilename = NULL;
            rc = pHlp->pfnCFGMQueryStringAlloc(pPluginCur, "Path", &pszPluginFilename);
            if (RT_SUCCESS(rc))
                rc = VDPluginLoadFromFilename(pszPluginFilename);

            if (RT_FAILURE(rc))
                LogRel(("VD: Failed to load plugin '%s' with %Rrc, continuing\n", pszPluginFilename, rc));

            pPluginCur = pHlp->pfnCFGMGetNextChild(pPluginCur);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Sets up the disk filter chain.
 *
 * @returns VBox status code.
 * @param   pThis    The disk instance.
 * @param   pCfg     CFGM node holding the filter parameters.
 */
static int drvvdSetupFilters(PVBOXDISK pThis, PCFGMNODE pCfg)
{
    PCPDMDRVHLPR3 pHlp = pThis->pDrvIns->pHlpR3;
    int rc = VINF_SUCCESS;

    PCFGMNODE pCfgFilter = pHlp->pfnCFGMGetChild(pCfg, "Filters");
    if (pCfgFilter)
    {
        PCFGMNODE pCfgFilterConfig = pHlp->pfnCFGMGetChild(pCfgFilter, "VDConfig");
        char *pszFilterName = NULL;
        VDINTERFACECONFIG VDIfConfig;
        PVDINTERFACE pVDIfsFilter = NULL;

        rc = pHlp->pfnCFGMQueryStringAlloc(pCfgFilter, "FilterName", &pszFilterName);
        if (RT_SUCCESS(rc))
        {
            VDCFGNODE CfgNode;

            VDIfConfig.pfnAreKeysValid = drvvdCfgAreKeysValid;
            VDIfConfig.pfnQuerySize    = drvvdCfgQuerySize;
            VDIfConfig.pfnQuery        = drvvdCfgQuery;
            VDIfConfig.pfnQueryBytes   = drvvdCfgQueryBytes;

            CfgNode.pHlp     = pThis->pDrvIns->pHlpR3;
            CfgNode.pCfgNode = pCfgFilterConfig;
            rc = VDInterfaceAdd(&VDIfConfig.Core, "DrvVD_Config", VDINTERFACETYPE_CONFIG,
                                &CfgNode, sizeof(VDINTERFACECONFIG), &pVDIfsFilter);
            AssertRC(rc);

            rc = VDFilterAdd(pThis->pDisk, pszFilterName, VD_FILTER_FLAGS_DEFAULT, pVDIfsFilter);

            PDMDrvHlpMMHeapFree(pThis->pDrvIns, pszFilterName);
        }
    }

    return rc;
}


/**
 * Translates a PDMMEDIATYPE value into a string.
 *
 * @returns Read only string.
 * @param   enmType             The type value.
 */
static const char *drvvdGetTypeName(PDMMEDIATYPE enmType)
{
    switch (enmType)
    {
        case PDMMEDIATYPE_ERROR:                return "ERROR";
        case PDMMEDIATYPE_FLOPPY_360:           return "FLOPPY_360";
        case PDMMEDIATYPE_FLOPPY_720:           return "FLOPPY_720";
        case PDMMEDIATYPE_FLOPPY_1_20:          return "FLOPPY_1_20";
        case PDMMEDIATYPE_FLOPPY_1_44:          return "FLOPPY_1_44";
        case PDMMEDIATYPE_FLOPPY_2_88:          return "FLOPPY_2_88";
        case PDMMEDIATYPE_FLOPPY_FAKE_15_6:     return "FLOPPY_FAKE_15_6";
        case PDMMEDIATYPE_FLOPPY_FAKE_63_5:     return "FLOPPY_FAKE_63_5";
        case PDMMEDIATYPE_CDROM:                return "CDROM";
        case PDMMEDIATYPE_DVD:                  return "DVD";
        case PDMMEDIATYPE_HARD_DISK:            return "HARD_DISK";
        default:                                return "Unknown";
    }
}

/**
 * Returns the appropriate PDMMEDIATYPE for t he given string.
 *
 * @returns PDMMEDIATYPE
 * @param   pszType    The string representation of the media type.
 */
static PDMMEDIATYPE drvvdGetMediaTypeFromString(const char *pszType)
{
    PDMMEDIATYPE enmType = PDMMEDIATYPE_ERROR;

    if (!strcmp(pszType, "HardDisk"))
        enmType = PDMMEDIATYPE_HARD_DISK;
    else if (!strcmp(pszType, "DVD"))
        enmType = PDMMEDIATYPE_DVD;
    else if (!strcmp(pszType, "CDROM"))
        enmType = PDMMEDIATYPE_CDROM;
    else if (!strcmp(pszType, "Floppy 2.88"))
        enmType = PDMMEDIATYPE_FLOPPY_2_88;
    else if (!strcmp(pszType, "Floppy 1.44"))
        enmType = PDMMEDIATYPE_FLOPPY_1_44;
    else if (!strcmp(pszType, "Floppy 1.20"))
        enmType = PDMMEDIATYPE_FLOPPY_1_20;
    else if (!strcmp(pszType, "Floppy 720"))
        enmType = PDMMEDIATYPE_FLOPPY_720;
    else if (!strcmp(pszType, "Floppy 360"))
        enmType = PDMMEDIATYPE_FLOPPY_360;
    else if (!strcmp(pszType, "Floppy 15.6"))
        enmType = PDMMEDIATYPE_FLOPPY_FAKE_15_6;
    else if (!strcmp(pszType, "Floppy 63.5"))
        enmType = PDMMEDIATYPE_FLOPPY_FAKE_63_5;

    return enmType;
}

/**
 * Converts PDMMEDIATYPE to the appropriate VDTYPE.
 *
 * @returns The VDTYPE.
 * @param   enmType    The PDMMEDIATYPE to convert from.
 */
static VDTYPE drvvdGetVDFromMediaType(PDMMEDIATYPE enmType)
{
    if (PDMMEDIATYPE_IS_FLOPPY(enmType))
        return VDTYPE_FLOPPY;
    else if (enmType == PDMMEDIATYPE_DVD || enmType == PDMMEDIATYPE_CDROM)
        return VDTYPE_OPTICAL_DISC;
    else if (enmType == PDMMEDIATYPE_HARD_DISK)
        return VDTYPE_HDD;

    AssertMsgFailed(("Invalid media type %d{%s} given!\n", enmType, drvvdGetTypeName(enmType)));
    return VDTYPE_HDD;
}

/**
 * Registers statistics associated with the given media driver.
 *
 * @returns VBox status code.
 * @param   pThis      The media driver instance.
 */
static int drvvdStatsRegister(PVBOXDISK pThis)
{
    PPDMDRVINS pDrvIns = pThis->pDrvIns;

    /*
     * Figure out where to place the stats.
     */
    uint32_t iInstance = 0;
    uint32_t iLUN = 0;
    const char *pcszController = NULL;
    int rc = pThis->pDrvMediaPort->pfnQueryDeviceLocation(pThis->pDrvMediaPort, &pcszController, &iInstance, &iLUN);
    AssertRCReturn(rc, rc);

    /*
     * Compose the prefix for the statistics to reduce the amount of repetition below.
     * The /Public/ bits are official and used by session info in the GUI.
     */
    char szCtrlUpper[32];
    rc = RTStrCopy(szCtrlUpper, sizeof(szCtrlUpper), pcszController);
    AssertRCReturn(rc, rc);

    RTStrToUpper(szCtrlUpper);
    char szPrefix[128];
    RTStrPrintf(szPrefix, sizeof(szPrefix), "/Public/Storage/%s%u/Port%u", szCtrlUpper, iInstance, iLUN);

    /*
     * Do the registrations.
     */
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatQueryBufAttempts,   STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Number of attempts to query a direct buffer.",              "%s/QueryBufAttempts", szPrefix);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatQueryBufSuccess,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Number of succeeded attempts to query a direct buffer.",    "%s/QueryBufSuccess", szPrefix);

    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatBytesRead,          STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                           "Amount of data read.",                          "%s/BytesRead", szPrefix);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatBytesWritten,       STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                           "Amount of data written.",                       "%s/BytesWritten", szPrefix);

    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsSubmitted,      STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                           "Number of I/O requests submitted.",             "%s/ReqsSubmitted", szPrefix);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsFailed,         STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                           "Number of I/O requests failed.",                "%s/ReqsFailed", szPrefix);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsSucceeded,      STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                           "Number of I/O requests succeeded.",             "%s/ReqsSucceeded", szPrefix);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsFlush,          STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                           "Number of flush I/O requests submitted.",       "%s/ReqsFlush", szPrefix);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsWrite,          STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                           "Number of write I/O requests submitted.",       "%s/ReqsWrite", szPrefix);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsRead,           STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                           "Number of read I/O requests submitted.",        "%s/ReqsRead", szPrefix);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsDiscard,        STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_COUNT,
                           "Number of discard I/O requests submitted.",     "%s/ReqsDiscard", szPrefix);

    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReqsPerSec,         STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                           "Number of processed I/O requests per second.",  "%s/ReqsPerSec", szPrefix);

    return VINF_SUCCESS;
}

/**
 * Deregisters statistics associated with the given media driver.
 *
 * @param   pThis      The media driver instance.
 */
static void drvvdStatsDeregister(PVBOXDISK pThis)
{
    PPDMDRVINS pDrvIns = pThis->pDrvIns;

    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatQueryBufAttempts);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatQueryBufSuccess);

    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatBytesRead);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatBytesWritten);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsSubmitted);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsFailed);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsSucceeded);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsFlush);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsWrite);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsRead);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsDiscard);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReqsPerSec);
}


/*********************************************************************************************************************************
*   Base interface methods                                                                                                       *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvvdQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PVBOXDISK   pThis   = PDMINS_2_DATA(pDrvIns, PVBOXDISK);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIA, &pThis->IMedia);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUNT, pThis->fMountable ? &pThis->IMount : NULL);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEX, pThis->pDrvMediaExPort ? &pThis->IMediaEx : NULL);
    return NULL;
}


/*********************************************************************************************************************************
*   Saved state notification methods                                                                                             *
*********************************************************************************************************************************/

/**
 * Load done callback for re-opening the image writable during teleportation.
 *
 * This is called both for successful and failed load runs, we only care about
 * successful ones.
 *
 * @returns VBox status code.
 * @param   pDrvIns         The driver instance.
 * @param   pSSM            The saved state handle.
 */
static DECLCALLBACK(int) drvvdLoadDone(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM)
{
    PVBOXDISK       pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;
    Assert(!pThis->fErrorUseRuntime);

    /* Drop out if we don't have any work to do or if it's a failed load. */
    if (   !pThis->fTempReadOnly
        || RT_FAILURE(pHlp->pfnSSMHandleGetStatus(pSSM)))
        return VINF_SUCCESS;

    int rc = drvvdSetWritable(pThis);
    if (RT_FAILURE(rc)) /** @todo does the bugger set any errors? */
        return pHlp->pfnSSMSetLoadError(pSSM, rc, RT_SRC_POS,
                                        N_("Failed to write lock the images"));
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Driver methods                                                                                                               *
*********************************************************************************************************************************/

/**
 * Worker for the power off or destruct callback.
 *
 * @param   pDrvIns    The driver instance.
 */
static void drvvdPowerOffOrDestructOrUnmount(PPDMDRVINS pDrvIns)
{
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    LogFlowFunc(("\n"));

    RTSEMFASTMUTEX mutex;
    ASMAtomicXchgHandle(&pThis->MergeCompleteMutex, NIL_RTSEMFASTMUTEX, &mutex);
    if (mutex != NIL_RTSEMFASTMUTEX)
    {
        /* Request the semaphore to wait until a potentially running merge
         * operation has been finished. */
        int rc = RTSemFastMutexRequest(mutex);
        AssertRC(rc);
        pThis->fMergePending = false;
        rc = RTSemFastMutexRelease(mutex);
        AssertRC(rc);
        rc = RTSemFastMutexDestroy(mutex);
        AssertRC(rc);
    }

    if (RT_VALID_PTR(pThis->pBlkCache))
    {
        PDMDrvHlpBlkCacheRelease(pThis->pDrvIns, pThis->pBlkCache);
        pThis->pBlkCache = NULL;
    }

    if (RT_VALID_PTR(pThis->pRegionList))
    {
        VDRegionListFree(pThis->pRegionList);
        pThis->pRegionList = NULL;
    }

    if (RT_VALID_PTR(pThis->pDisk))
    {
        VDDestroy(pThis->pDisk);
        pThis->pDisk = NULL;
    }
    drvvdFreeImages(pThis);
}

/**
 * @copydoc FNPDMDRVPOWEROFF
 */
static DECLCALLBACK(void) drvvdPowerOff(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    drvvdPowerOffOrDestructOrUnmount(pDrvIns);
}

/**
 * @callback_method_impl{FNPDMDRVRESUME}
 *
 * VM resume notification that we use to undo what the temporary read-only image
 * mode set by drvvdSuspend.
 *
 * Also switch to runtime error mode if we're resuming after a state load
 * without having been powered on first.
 *
 * @todo    The VMSetError vs VMSetRuntimeError mess must be fixed elsewhere,
 *          we're making assumptions about Main behavior here!
 */
static DECLCALLBACK(void) drvvdResume(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);

    drvvdSetWritable(pThis);
    pThis->fSuspending      = false;
    pThis->fRedo            = false;

    if (pThis->pBlkCache)
    {
        int rc = PDMDrvHlpBlkCacheResume(pThis->pDrvIns, pThis->pBlkCache);
        AssertRC(rc);
    }

    if (pThis->pDrvMediaExPort)
    {
        /* Mark all requests waiting for I/O memory as active again so they get accounted for. */
        RTCritSectEnter(&pThis->CritSectIoReqsIoBufWait);
        PPDMMEDIAEXIOREQINT pIoReq, pIoReqNext;
        RTListForEachSafe(&pThis->LstIoReqIoBufWait, pIoReq, pIoReqNext, PDMMEDIAEXIOREQINT, NdLstWait)
        {
            pThis->pDrvMediaExPort->pfnIoReqStateChanged(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                         PDMMEDIAEXIOREQSTATE_ACTIVE);
            ASMAtomicIncU32(&pThis->cIoReqsActive);
            LogFlowFunc(("Resumed I/O request %#p\n", pIoReq));
        }
        RTCritSectLeave(&pThis->CritSectIoReqsIoBufWait);

        /* Kick of any request we have to redo. */
        RTCritSectEnter(&pThis->CritSectIoReqRedo);
        RTListForEachSafe(&pThis->LstIoReqRedo, pIoReq, pIoReqNext, PDMMEDIAEXIOREQINT, NdLstWait)
        {
            int rc = VINF_SUCCESS;
            bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pIoReq->enmState, VDIOREQSTATE_ACTIVE, VDIOREQSTATE_SUSPENDED);

            RTListNodeRemove(&pIoReq->NdLstWait);
            ASMAtomicIncU32(&pThis->cIoReqsActive);

            LogFlowFunc(("Resuming I/O request %#p fXchg=%RTbool\n", pIoReq, fXchg));
            if (fXchg)
            {
                pThis->pDrvMediaExPort->pfnIoReqStateChanged(pThis->pDrvMediaExPort, pIoReq, &pIoReq->abAlloc[0],
                                                             PDMMEDIAEXIOREQSTATE_ACTIVE);
                LogFlowFunc(("Resumed I/O request %#p\n", pIoReq));
                if (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_READ
                    || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
                    rc = drvvdMediaExIoReqReadWriteProcess(pThis, pIoReq, true /* fUpNotify */);
                else if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
                {
                    rc = drvvdMediaExIoReqFlushWrapper(pThis, pIoReq);
                    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
                        rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
                    else if (rc == VINF_VD_ASYNC_IO_FINISHED)
                        rc = VINF_SUCCESS;
                }
                else if (pIoReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
                {
                    rc = drvvdMediaExIoReqDiscardWrapper(pThis, pIoReq);
                    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
                        rc = VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS;
                    else if (rc == VINF_VD_ASYNC_IO_FINISHED)
                        rc = VINF_SUCCESS;
                }
                else
                    AssertMsgFailed(("Invalid request type %u\n", pIoReq->enmType));

                /* The read write process will call the completion callback on its own. */
                if (   rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS
                    && (   pIoReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD
                        || pIoReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH))
                {
                    Assert(   (   pIoReq->enmType != PDMMEDIAEXIOREQTYPE_WRITE
                               && pIoReq->enmType != PDMMEDIAEXIOREQTYPE_READ)
                           || !pIoReq->ReadWrite.cbReqLeft
                           || RT_FAILURE(rc));
                    drvvdMediaExIoReqCompleteWorker(pThis, pIoReq, rc, true /* fUpNotify */);
                }

            }
            else
            {
                /* Request was canceled inbetween, so don't care and notify the owner about the completed request. */
                Assert(pIoReq->enmState == VDIOREQSTATE_CANCELED);
                drvvdMediaExIoReqCompleteWorker(pThis, pIoReq, VERR_PDM_MEDIAEX_IOREQ_CANCELED, true /* fUpNotify */);
            }
        }
        Assert(RTListIsEmpty(&pThis->LstIoReqRedo));
        RTCritSectLeave(&pThis->CritSectIoReqRedo);
    }

    /* Try to process any requests waiting for I/O memory now. */
    drvvdMediaExIoReqProcessWaiting(pThis);
    pThis->fErrorUseRuntime = true;
}

/**
 * @callback_method_impl{FNPDMDRVSUSPEND}
 *
 * When the VM is being suspended, temporarily change to read-only image mode.
 *
 * This is important for several reasons:
 *   -# It makes sure that there are no pending writes to the image.  Most
 *      backends implements this by closing and reopening the image in read-only
 *      mode.
 *   -# It allows Main to read the images during snapshotting without having
 *      to account for concurrent writes.
 *   -# This is essential for making teleportation targets sharing images work
 *      right.  Both with regards to caching and with regards to file sharing
 *      locks (RTFILE_O_DENY_*).  (See also drvvdLoadDone.)
 */
static DECLCALLBACK(void) drvvdSuspend(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);

    if (pThis->pBlkCache)
    {
        int rc = PDMDrvHlpBlkCacheSuspend(pThis->pDrvIns, pThis->pBlkCache);
        AssertRC(rc);
    }

    drvvdSetReadonly(pThis);
}

/**
 * @callback_method_impl{FNPDMDRVPOWERON}
 */
static DECLCALLBACK(void) drvvdPowerOn(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    drvvdSetWritable(pThis);
    pThis->fErrorUseRuntime = true;
}

/**
 * @callback_method_impl{FNPDMDRVRESET}
 */
static DECLCALLBACK(void) drvvdReset(PPDMDRVINS pDrvIns)
{
    LogFlowFunc(("\n"));
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);

    if (pThis->pBlkCache)
    {
        int rc = PDMDrvHlpBlkCacheClear(pThis->pDrvIns, pThis->pBlkCache);
        AssertRC(rc);
    }

    if (pThis->fBootAccelEnabled)
    {
        pThis->fBootAccelActive = true;
        pThis->cbDataValid      = 0;
        pThis->offDisk          = 0;
    }
    pThis->fLocked = false;
}

/**
 * @callback_method_impl{FNPDMDRVDESTRUCT}
 */
static DECLCALLBACK(void) drvvdDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PVBOXDISK pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    LogFlowFunc(("\n"));

    /*
     * Make sure the block cache and disks are closed when this driver is
     * destroyed. This method will get called without calling the power off
     * callback first when we reconfigure the driver chain after a snapshot.
     */
    drvvdPowerOffOrDestructOrUnmount(pDrvIns);
    if (pThis->MergeLock != NIL_RTSEMRW)
    {
        int rc = RTSemRWDestroy(pThis->MergeLock);
        AssertRC(rc);
        pThis->MergeLock = NIL_RTSEMRW;
    }
    if (pThis->pbData)
    {
        RTMemFree(pThis->pbData);
        pThis->pbData = NULL;
    }
    if (pThis->pszBwGroup)
    {
        PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszBwGroup);
        pThis->pszBwGroup = NULL;
    }
    if (pThis->hHbdMgr != NIL_HBDMGR)
        HBDMgrDestroy(pThis->hHbdMgr);
    if (pThis->hIoReqCache != NIL_RTMEMCACHE)
        RTMemCacheDestroy(pThis->hIoReqCache);
    if (pThis->hIoBufMgr != NIL_IOBUFMGR)
        IOBUFMgrDestroy(pThis->hIoBufMgr);
    if (RTCritSectIsInitialized(&pThis->CritSectIoReqsIoBufWait))
        RTCritSectDelete(&pThis->CritSectIoReqsIoBufWait);
    if (RTCritSectIsInitialized(&pThis->CritSectIoReqRedo))
        RTCritSectDelete(&pThis->CritSectIoReqRedo);
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIoReqAllocBins); i++)
        if (pThis->aIoReqAllocBins[i].hMtxLstIoReqAlloc != NIL_RTSEMFASTMUTEX)
            RTSemFastMutexDestroy(pThis->aIoReqAllocBins[i].hMtxLstIoReqAlloc);

    drvvdStatsDeregister(pThis);

    PVDCFGNODE pIt;
    PVDCFGNODE pItNext;
    RTListForEachSafe(&pThis->LstCfgNodes, pIt, pItNext, VDCFGNODE, NdLst)
    {
        RTListNodeRemove(&pIt->NdLst);
        RTMemFreeZ(pIt, sizeof(*pIt));
    }
}

/**
 * @callback_method_impl{FNPDMDRVCONSTRUCT,
 *      Construct a VBox disk media driver instance.}
 */
static DECLCALLBACK(int) drvvdConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PVBOXDISK       pThis = PDMINS_2_DATA(pDrvIns, PVBOXDISK);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    LogFlowFunc(("\n"));

    char *pszName = NULL;        /* The path of the disk image file. */
    char *pszFormat = NULL;      /* The format backed to use for this image. */
    char *pszCachePath = NULL;   /* The path to the cache image. */
    char *pszCacheFormat = NULL; /* The format backend to use for the cache image. */
    bool fReadOnly = false;      /* True if the media is read-only. */
    bool fMaybeReadOnly = false; /* True if the media may or may not be read-only. */
    bool fHonorZeroWrites = false; /* True if zero blocks should be written. */

    /*
     * Init the static parts.
     */
    pDrvIns->IBase.pfnQueryInterface    = drvvdQueryInterface;
    pThis->pDrvIns                      = pDrvIns;
    pThis->fTempReadOnly                = false;
    pThis->pDisk                        = NULL;
    pThis->fAsyncIOSupported            = false;
    pThis->fShareable                   = false;
    pThis->fMergePending                = false;
    pThis->MergeCompleteMutex           = NIL_RTSEMFASTMUTEX;
    pThis->MergeLock                    = NIL_RTSEMRW;
    pThis->uMergeSource                 = VD_LAST_IMAGE;
    pThis->uMergeTarget                 = VD_LAST_IMAGE;
    pThis->CfgCrypto.pCfgNode           = NULL;
    pThis->CfgCrypto.pHlp               = pDrvIns->pHlpR3;
    pThis->pIfSecKey                    = NULL;
    pThis->hIoReqCache                  = NIL_RTMEMCACHE;
    pThis->hIoBufMgr                    = NIL_IOBUFMGR;
    pThis->pRegionList                  = NULL;
    pThis->fSuspending                  = false;
    pThis->fRedo                        = false;

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIoReqAllocBins); i++)
        pThis->aIoReqAllocBins[i].hMtxLstIoReqAlloc = NIL_RTSEMFASTMUTEX;

    /* IMedia */
    pThis->IMedia.pfnRead                        = drvvdRead;
    pThis->IMedia.pfnReadPcBios                  = drvvdReadPcBios;
    pThis->IMedia.pfnWrite                       = drvvdWrite;
    pThis->IMedia.pfnFlush                       = drvvdFlush;
    pThis->IMedia.pfnMerge                       = drvvdMerge;
    pThis->IMedia.pfnSetSecKeyIf                 = drvvdSetSecKeyIf;
    pThis->IMedia.pfnGetSize                     = drvvdGetSize;
    pThis->IMedia.pfnGetSectorSize               = drvvdGetSectorSize;
    pThis->IMedia.pfnIsReadOnly                  = drvvdIsReadOnly;
    pThis->IMedia.pfnIsNonRotational             = drvvdIsNonRotational;
    pThis->IMedia.pfnBiosGetPCHSGeometry         = drvvdBiosGetPCHSGeometry;
    pThis->IMedia.pfnBiosSetPCHSGeometry         = drvvdBiosSetPCHSGeometry;
    pThis->IMedia.pfnBiosGetLCHSGeometry         = drvvdBiosGetLCHSGeometry;
    pThis->IMedia.pfnBiosSetLCHSGeometry         = drvvdBiosSetLCHSGeometry;
    pThis->IMedia.pfnBiosIsVisible               = drvvdBiosIsVisible;
    pThis->IMedia.pfnGetType                     = drvvdGetType;
    pThis->IMedia.pfnGetUuid                     = drvvdGetUuid;
    pThis->IMedia.pfnDiscard                     = drvvdDiscard;
    pThis->IMedia.pfnSendCmd                     = NULL;
    pThis->IMedia.pfnGetRegionCount              = drvvdGetRegionCount;
    pThis->IMedia.pfnQueryRegionProperties       = drvvdQueryRegionProperties;
    pThis->IMedia.pfnQueryRegionPropertiesForLba = drvvdQueryRegionPropertiesForLba;

    /* IMount */
    pThis->IMount.pfnUnmount                = drvvdUnmount;
    pThis->IMount.pfnIsMounted              = drvvdIsMounted;
    pThis->IMount.pfnLock                   = drvvdLock;
    pThis->IMount.pfnUnlock                 = drvvdUnlock;
    pThis->IMount.pfnIsLocked               = drvvdIsLocked;

    /* IMediaEx */
    pThis->IMediaEx.pfnQueryFeatures            = drvvdQueryFeatures;
    pThis->IMediaEx.pfnNotifySuspend            = drvvdNotifySuspend;
    pThis->IMediaEx.pfnIoReqAllocSizeSet        = drvvdIoReqAllocSizeSet;
    pThis->IMediaEx.pfnIoReqAlloc               = drvvdIoReqAlloc;
    pThis->IMediaEx.pfnIoReqFree                = drvvdIoReqFree;
    pThis->IMediaEx.pfnIoReqQueryResidual       = drvvdIoReqQueryResidual;
    pThis->IMediaEx.pfnIoReqQueryXferSize       = drvvdIoReqQueryXferSize;
    pThis->IMediaEx.pfnIoReqCancelAll           = drvvdIoReqCancelAll;
    pThis->IMediaEx.pfnIoReqCancel              = drvvdIoReqCancel;
    pThis->IMediaEx.pfnIoReqRead                = drvvdIoReqRead;
    pThis->IMediaEx.pfnIoReqWrite               = drvvdIoReqWrite;
    pThis->IMediaEx.pfnIoReqFlush               = drvvdIoReqFlush;
    pThis->IMediaEx.pfnIoReqDiscard             = drvvdIoReqDiscard;
    pThis->IMediaEx.pfnIoReqSendScsiCmd         = drvvdIoReqSendScsiCmd;
    pThis->IMediaEx.pfnIoReqGetActiveCount      = drvvdIoReqGetActiveCount;
    pThis->IMediaEx.pfnIoReqGetSuspendedCount   = drvvdIoReqGetSuspendedCount;
    pThis->IMediaEx.pfnIoReqQuerySuspendedStart = drvvdIoReqQuerySuspendedStart;
    pThis->IMediaEx.pfnIoReqQuerySuspendedNext  = drvvdIoReqQuerySuspendedNext;
    pThis->IMediaEx.pfnIoReqSuspendedSave       = drvvdIoReqSuspendedSave;
    pThis->IMediaEx.pfnIoReqSuspendedLoad       = drvvdIoReqSuspendedLoad;

    RTListInit(&pThis->LstCfgNodes);

    /* Initialize supported VD interfaces. */
    pThis->pVDIfsDisk = NULL;

    pThis->VDIfError.pfnError     = drvvdErrorCallback;
    pThis->VDIfError.pfnMessage   = NULL;
    int rc = VDInterfaceAdd(&pThis->VDIfError.Core, "DrvVD_VDIError", VDINTERFACETYPE_ERROR,
                            pDrvIns, sizeof(VDINTERFACEERROR), &pThis->pVDIfsDisk);
    AssertRC(rc);

    /* List of images is empty now. */
    pThis->pImages = NULL;

    pThis->pDrvMediaPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAPORT);
    if (!pThis->pDrvMediaPort)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE,
                                N_("No media port interface above"));

    pThis->pDrvMountNotify    = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMOUNTNOTIFY);

    /*
     * Try to attach the optional extended media interface port above and initialize associated
     * structures if available.
     */
    pThis->pDrvMediaExPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMEDIAEXPORT);
    if (pThis->pDrvMediaExPort)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIoReqAllocBins); i++)
        {
            rc = RTSemFastMutexCreate(&pThis->aIoReqAllocBins[i].hMtxLstIoReqAlloc);
            if (RT_FAILURE(rc))
                break;
            RTListInit(&pThis->aIoReqAllocBins[i].LstIoReqAlloc);
        }

        if (RT_SUCCESS(rc))
            rc = RTCritSectInit(&pThis->CritSectIoReqsIoBufWait);

        if (RT_SUCCESS(rc))
            rc = RTCritSectInit(&pThis->CritSectIoReqRedo);

        if (RT_FAILURE(rc))
            return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Creating Mutex failed"));

        RTListInit(&pThis->LstIoReqIoBufWait);
        RTListInit(&pThis->LstIoReqRedo);
    }

    /* Before we access any VD API load all given plugins. */
    rc = drvvdLoadPlugins(pDrvIns, pCfg);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Loading VD plugins failed"));

    /*
     * Validate configuration and find all parent images.
     * It's sort of up side down from the image dependency tree.
     */
    bool        fHostIP = false;
    bool        fUseNewIo = false;
    bool        fUseBlockCache = false;
    bool        fDiscard = false;
    bool        fInformAboutZeroBlocks = false;
    bool        fSkipConsistencyChecks = false;
    bool        fEmptyDrive            = false;
    unsigned    iLevel = 0;
    PCFGMNODE   pCurNode = pCfg;
    uint32_t    cbIoBufMax = 0;

    for (;;)
    {
        bool fValid;

        if (pCurNode == pCfg)
        {
            /* Toplevel configuration additionally contains the global image
             * open flags. Some might be converted to per-image flags later. */
            fValid = pHlp->pfnCFGMAreValuesValid(pCurNode,
                                                 "Format\0Path\0"
                                                 "ReadOnly\0MaybeReadOnly\0TempReadOnly\0Shareable\0HonorZeroWrites\0"
                                                 "HostIPStack\0UseNewIo\0BootAcceleration\0BootAccelerationBuffer\0"
                                                 "SetupMerge\0MergeSource\0MergeTarget\0BwGroup\0Type\0BlockCache\0"
                                                 "CachePath\0CacheFormat\0Discard\0InformAboutZeroBlocks\0"
                                                 "SkipConsistencyChecks\0"
                                                 "Locked\0BIOSVisible\0Cylinders\0Heads\0Sectors\0Mountable\0"
                                                 "EmptyDrive\0IoBufMax\0NonRotationalMedium\0"
#if defined(VBOX_PERIODIC_FLUSH) || defined(VBOX_IGNORE_FLUSH)
                                                 "FlushInterval\0IgnoreFlush\0IgnoreFlushAsync\0"
#endif /* !(VBOX_PERIODIC_FLUSH || VBOX_IGNORE_FLUSH) */
                                           );
        }
        else
        {
            /* All other image configurations only contain image name and
             * the format information. */
            fValid = pHlp->pfnCFGMAreValuesValid(pCurNode, "Format\0Path\0"
                                                           "MergeSource\0MergeTarget\0");
        }
        if (!fValid)
        {
            rc = PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES,
                                     RT_SRC_POS, N_("DrvVD: Configuration error: keys incorrect at level %d"), iLevel);
            break;
        }

        if (pCurNode == pCfg)
        {
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "HostIPStack", &fHostIP, true);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"HostIPStack\" as boolean failed"));
                break;
            }

            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "HonorZeroWrites", &fHonorZeroWrites, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"HonorZeroWrites\" as boolean failed"));
                break;
            }

            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "ReadOnly", &fReadOnly, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"ReadOnly\" as boolean failed"));
                break;
            }

            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "MaybeReadOnly", &fMaybeReadOnly, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"MaybeReadOnly\" as boolean failed"));
                break;
            }

            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "TempReadOnly", &pThis->fTempReadOnly, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"TempReadOnly\" as boolean failed"));
                break;
            }
            if (fReadOnly && pThis->fTempReadOnly)
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRIVER_INVALID_PROPERTIES,
                                      N_("DrvVD: Configuration error: Both \"ReadOnly\" and \"TempReadOnly\" are set"));
                break;
            }

            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "Shareable", &pThis->fShareable, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"Shareable\" as boolean failed"));
                break;
            }

            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "UseNewIo", &fUseNewIo, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"UseNewIo\" as boolean failed"));
                break;
            }
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "SetupMerge", &pThis->fMergePending, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"SetupMerge\" as boolean failed"));
                break;
            }
            if (fReadOnly && pThis->fMergePending)
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRIVER_INVALID_PROPERTIES,
                                      N_("DrvVD: Configuration error: Both \"ReadOnly\" and \"MergePending\" are set"));
                break;
            }
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "BootAcceleration", &pThis->fBootAccelEnabled, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"BootAcceleration\" as boolean failed"));
                break;
            }
            rc = pHlp->pfnCFGMQueryU32Def(pCurNode, "BootAccelerationBuffer", (uint32_t *)&pThis->cbBootAccelBuffer, 16 * _1K);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"BootAccelerationBuffer\" as integer failed"));
                break;
            }
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "BlockCache", &fUseBlockCache, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"BlockCache\" as boolean failed"));
                break;
            }
            rc = pHlp->pfnCFGMQueryStringAlloc(pCurNode, "BwGroup", &pThis->pszBwGroup);
            if (RT_FAILURE(rc) && rc != VERR_CFGM_VALUE_NOT_FOUND)
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"BwGroup\" as string failed"));
                break;
            }
            else
                rc = VINF_SUCCESS;
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "Discard", &fDiscard, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"Discard\" as boolean failed"));
                break;
            }
            if (fReadOnly && fDiscard)
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRIVER_INVALID_PROPERTIES,
                                      N_("DrvVD: Configuration error: Both \"ReadOnly\" and \"Discard\" are set"));
                break;
            }
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "InformAboutZeroBlocks", &fInformAboutZeroBlocks, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"InformAboutZeroBlocks\" as boolean failed"));
                break;
            }
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "SkipConsistencyChecks", &fSkipConsistencyChecks, true);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"SKipConsistencyChecks\" as boolean failed"));
                break;
            }

            char *psz = NULL;
            rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "Type", &psz);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_BLOCK_NO_TYPE, N_("Failed to obtain the sub type"));
            pThis->enmType = drvvdGetMediaTypeFromString(psz);
            if (pThis->enmType == PDMMEDIATYPE_ERROR)
            {
                PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_BLOCK_UNKNOWN_TYPE, RT_SRC_POS,
                                    N_("Unknown type \"%s\""), psz);
                PDMDrvHlpMMHeapFree(pDrvIns, psz);
                return VERR_PDM_BLOCK_UNKNOWN_TYPE;
            }
            PDMDrvHlpMMHeapFree(pDrvIns, psz); psz = NULL;

            rc = pHlp->pfnCFGMQueryStringAlloc(pCurNode, "CachePath", &pszCachePath);
            if (RT_FAILURE(rc) && rc != VERR_CFGM_VALUE_NOT_FOUND)
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"CachePath\" as string failed"));
                break;
            }
            rc = VINF_SUCCESS;

            if (pszCachePath)
            {
                rc = pHlp->pfnCFGMQueryStringAlloc(pCurNode, "CacheFormat", &pszCacheFormat);
                if (RT_FAILURE(rc))
                {
                    rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                          N_("DrvVD: Configuration error: Querying \"CacheFormat\" as string failed"));
                    break;
                }
            }

            /* Mountable */
            rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Mountable", &pThis->fMountable, false);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"Mountable\" from the config"));

            /* Locked */
            rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Locked", &pThis->fLocked, false);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"Locked\" from the config"));

            /* BIOS visible */
            rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "BIOSVisible", &pThis->fBiosVisible, true);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"BIOSVisible\" from the config"));

            /* Cylinders */
            rc = pHlp->pfnCFGMQueryU32Def(pCfg, "Cylinders", &pThis->LCHSGeometry.cCylinders, 0);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"Cylinders\" from the config"));

            /* Heads */
            rc = pHlp->pfnCFGMQueryU32Def(pCfg, "Heads", &pThis->LCHSGeometry.cHeads, 0);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"Heads\" from the config"));

            /* Sectors */
            rc = pHlp->pfnCFGMQueryU32Def(pCfg, "Sectors", &pThis->LCHSGeometry.cSectors, 0);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"Sectors\" from the config"));

            /* Uuid */
            rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "Uuid", &psz);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                RTUuidClear(&pThis->Uuid);
            else if (RT_SUCCESS(rc))
            {
                rc = RTUuidFromStr(&pThis->Uuid, psz);
                if (RT_FAILURE(rc))
                {
                    PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("Uuid from string failed on \"%s\""), psz);
                    PDMDrvHlpMMHeapFree(pDrvIns, psz);
                    return rc;
                }
                PDMDrvHlpMMHeapFree(pDrvIns, psz); psz = NULL;
            }
            else
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"Uuid\" from the config"));

#ifdef VBOX_PERIODIC_FLUSH
            rc = pHlp->pfnCFGMQueryU32Def(pCfg, "FlushInterval", &pThis->cbFlushInterval, 0);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"FlushInterval\" from the config"));
#endif /* VBOX_PERIODIC_FLUSH */

#ifdef VBOX_IGNORE_FLUSH
            rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IgnoreFlush", &pThis->fIgnoreFlush, true);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"IgnoreFlush\" from the config"));

            if (pThis->fIgnoreFlush)
                LogRel(("DrvVD: Flushes will be ignored\n"));
            else
                LogRel(("DrvVD: Flushes will be passed to the disk\n"));

            rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "IgnoreFlushAsync", &pThis->fIgnoreFlushAsync, false);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"IgnoreFlushAsync\" from the config"));

            if (pThis->fIgnoreFlushAsync)
                LogRel(("DrvVD: Async flushes will be ignored\n"));
            else
                LogRel(("DrvVD: Async flushes will be passed to the disk\n"));
#endif /* VBOX_IGNORE_FLUSH */

            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "EmptyDrive", &fEmptyDrive, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"EmptyDrive\" as boolean failed"));
                break;
            }

            rc = pHlp->pfnCFGMQueryU32Def(pCfg, "IoBufMax", &cbIoBufMax, 5 * _1M);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Failed to query \"IoBufMax\" from the config"));

            rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "NonRotationalMedium", &pThis->fNonRotational, false);
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc,
                                        N_("DrvVD configuration error: Querying \"NonRotationalMedium\" as boolean failed"));
        }

        PCFGMNODE pParent = pHlp->pfnCFGMGetChild(pCurNode, "Parent");
        if (!pParent)
            break;
        pCurNode = pParent;
        iLevel++;
    }

    if (pThis->pDrvMediaExPort)
        rc = IOBUFMgrCreate(&pThis->hIoBufMgr, cbIoBufMax, pThis->CfgCrypto.pCfgNode ? IOBUFMGR_F_REQUIRE_NOT_PAGABLE : IOBUFMGR_F_DEFAULT);

    if (   !fEmptyDrive
        && RT_SUCCESS(rc))
    {
        /*
         * Create the image container and the necessary interfaces.
         */
        if (RT_SUCCESS(rc))
        {
            /*
             * The image has a bandwidth group but the host cache is enabled.
             * Use the async I/O framework but tell it to enable the host cache.
             */
            if (!fUseNewIo && pThis->pszBwGroup)
            {
                pThis->fAsyncIoWithHostCache = true;
                fUseNewIo = true;
            }

            /** @todo quick hack to work around problems in the async I/O
             * implementation (rw semaphore thread ownership problem)
             * while a merge is running. Remove once this is fixed. */
            if (pThis->fMergePending)
                fUseNewIo = false;

            if (RT_SUCCESS(rc) && pThis->fMergePending)
            {
                rc = RTSemFastMutexCreate(&pThis->MergeCompleteMutex);
                if (RT_SUCCESS(rc))
                    rc = RTSemRWCreate(&pThis->MergeLock);
                if (RT_SUCCESS(rc))
                {
                    pThis->VDIfThreadSync.pfnStartRead   = drvvdThreadStartRead;
                    pThis->VDIfThreadSync.pfnFinishRead  = drvvdThreadFinishRead;
                    pThis->VDIfThreadSync.pfnStartWrite  = drvvdThreadStartWrite;
                    pThis->VDIfThreadSync.pfnFinishWrite = drvvdThreadFinishWrite;

                    rc = VDInterfaceAdd(&pThis->VDIfThreadSync.Core, "DrvVD_ThreadSync", VDINTERFACETYPE_THREADSYNC,
                                        pThis, sizeof(VDINTERFACETHREADSYNC), &pThis->pVDIfsDisk);
                }
                else
                {
                    rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                          N_("DrvVD: Failed to create semaphores for \"MergePending\""));
                }
            }

            if (RT_SUCCESS(rc))
            {
                rc = VDCreate(pThis->pVDIfsDisk, drvvdGetVDFromMediaType(pThis->enmType), &pThis->pDisk);
                /* Error message is already set correctly. */
            }
        }

        if (pThis->pDrvMediaExPort && fUseNewIo)
            pThis->fAsyncIOSupported = true;

        uint64_t tsStart = RTTimeNanoTS();

        unsigned iImageIdx = 0;
        while (pCurNode && RT_SUCCESS(rc))
        {
            /* Allocate per-image data. */
            PVBOXIMAGE pImage = drvvdNewImage(pThis);
            if (!pImage)
            {
                rc = VERR_NO_MEMORY;
                break;
            }

            /*
             * Read the image configuration.
             */
            rc = pHlp->pfnCFGMQueryStringAlloc(pCurNode, "Path", &pszName);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"Path\" as string failed"));
                break;
            }

            rc = pHlp->pfnCFGMQueryStringAlloc(pCurNode, "Format", &pszFormat);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"Format\" as string failed"));
                break;
            }

            bool fMergeSource;
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "MergeSource", &fMergeSource, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"MergeSource\" as boolean failed"));
                break;
            }
            if (fMergeSource)
            {
                if (pThis->uMergeSource == VD_LAST_IMAGE)
                    pThis->uMergeSource = iImageIdx;
                else
                {
                    rc = PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRIVER_INVALID_PROPERTIES,
                                          N_("DrvVD: Configuration error: Multiple \"MergeSource\" occurrences"));
                    break;
                }
            }

            bool fMergeTarget;
            rc = pHlp->pfnCFGMQueryBoolDef(pCurNode, "MergeTarget", &fMergeTarget, false);
            if (RT_FAILURE(rc))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, rc,
                                      N_("DrvVD: Configuration error: Querying \"MergeTarget\" as boolean failed"));
                break;
            }
            if (fMergeTarget)
            {
                if (pThis->uMergeTarget == VD_LAST_IMAGE)
                    pThis->uMergeTarget = iImageIdx;
                else
                {
                    rc = PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRIVER_INVALID_PROPERTIES,
                                          N_("DrvVD: Configuration error: Multiple \"MergeTarget\" occurrences"));
                    break;
                }
            }

            PCFGMNODE pCfgVDConfig = pHlp->pfnCFGMGetChild(pCurNode, "VDConfig");
            pImage->VDIfConfig.pfnAreKeysValid = drvvdCfgAreKeysValid;
            pImage->VDIfConfig.pfnQuerySize    = drvvdCfgQuerySize;
            pImage->VDIfConfig.pfnQuery        = drvvdCfgQuery;
            pImage->VDIfConfig.pfnQueryBytes   = NULL;

            PVDCFGNODE pCfgNode = (PVDCFGNODE)RTMemAllocZ(sizeof(*pCfgNode));
            if (RT_UNLIKELY(!pCfgNode))
            {
                rc = PDMDRV_SET_ERROR(pDrvIns, VERR_NO_MEMORY,
                                      N_("DrvVD: Failed to allocate memory for config node"));
                break;
            }

            pCfgNode->pHlp     = pDrvIns->pHlpR3;
            pCfgNode->pCfgNode = pCfgVDConfig;
            RTListAppend(&pThis->LstCfgNodes, &pCfgNode->NdLst);

            rc = VDInterfaceAdd(&pImage->VDIfConfig.Core, "DrvVD_Config", VDINTERFACETYPE_CONFIG,
                                pCfgNode, sizeof(VDINTERFACECONFIG), &pImage->pVDIfsImage);
            AssertRC(rc);

            /* Check VDConfig for encryption config. */
            /** @todo This makes sure that the crypto config is not cleared accidentally
             * when it was set because there are multiple VDConfig entries for a snapshot chain
             * but only one contains the crypto config.
             *
             * This needs to be properly fixed by specifying which part of the image should contain the
             * crypto stuff.
             */
            if (!pThis->CfgCrypto.pCfgNode)
            {
                if (pCfgVDConfig)
                    pThis->CfgCrypto.pCfgNode = pHlp->pfnCFGMGetChild(pCfgVDConfig, "CRYPT");

                if (pThis->CfgCrypto.pCfgNode)
                {
                    /* Setup VDConfig interface for disk encryption support. */
                    pThis->VDIfCfg.pfnAreKeysValid  = drvvdCfgAreKeysValid;
                    pThis->VDIfCfg.pfnQuerySize     = drvvdCfgQuerySize;
                    pThis->VDIfCfg.pfnQuery         = drvvdCfgQuery;
                    pThis->VDIfCfg.pfnQueryBytes    = NULL;

                    pThis->VDIfCrypto.pfnKeyRetain               = drvvdCryptoKeyRetain;
                    pThis->VDIfCrypto.pfnKeyRelease              = drvvdCryptoKeyRelease;
                    pThis->VDIfCrypto.pfnKeyStorePasswordRetain  = drvvdCryptoKeyStorePasswordRetain;
                    pThis->VDIfCrypto.pfnKeyStorePasswordRelease = drvvdCryptoKeyStorePasswordRelease;
                }
            }

            /* Unconditionally insert the TCPNET interface, don't bother to check
             * if an image really needs it. Will be ignored. Since the TCPNET
             * interface is per image we could make this more flexible in the
             * future if we want to. */
            /* Construct TCPNET callback table depending on the config. This is
             * done unconditionally, as uninterested backends will ignore it. */
            if (fHostIP)
                rc = VDIfTcpNetInstDefaultCreate(&pImage->hVdIfTcpNet, &pImage->pVDIfsImage);
            else
            {
#ifndef VBOX_WITH_INIP
                rc = PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES,
                                         RT_SRC_POS, N_("DrvVD: Configuration error: TCP over Internal Networking not compiled in"));
#else /* VBOX_WITH_INIP */
                pImage->VDIfTcpNet.pfnSocketCreate = drvvdINIPSocketCreate;
                pImage->VDIfTcpNet.pfnSocketDestroy = drvvdINIPSocketDestroy;
                pImage->VDIfTcpNet.pfnClientConnect = drvvdINIPClientConnect;
                pImage->VDIfTcpNet.pfnClientClose = drvvdINIPClientClose;
                pImage->VDIfTcpNet.pfnIsClientConnected = drvvdINIPIsClientConnected;
                pImage->VDIfTcpNet.pfnSelectOne = drvvdINIPSelectOne;
                pImage->VDIfTcpNet.pfnRead = drvvdINIPRead;
                pImage->VDIfTcpNet.pfnWrite = drvvdINIPWrite;
                pImage->VDIfTcpNet.pfnSgWrite = drvvdINIPSgWrite;
                pImage->VDIfTcpNet.pfnFlush = drvvdINIPFlush;
                pImage->VDIfTcpNet.pfnSetSendCoalescing = drvvdINIPSetSendCoalescing;
                pImage->VDIfTcpNet.pfnGetLocalAddress = drvvdINIPGetLocalAddress;
                pImage->VDIfTcpNet.pfnGetPeerAddress = drvvdINIPGetPeerAddress;
                pImage->VDIfTcpNet.pfnSelectOneEx = drvvdINIPSelectOneEx;
                pImage->VDIfTcpNet.pfnPoke = drvvdINIPPoke;

                rc = VDInterfaceAdd(&pImage->VDIfTcpNet.Core, "DrvVD_TCPNET",
                                    VDINTERFACETYPE_TCPNET, NULL,
                                    sizeof(VDINTERFACETCPNET), &pImage->pVDIfsImage);
                AssertRC(rc);
#endif /* VBOX_WITH_INIP */
            }

            /* Insert the custom I/O interface only if we're told to use new IO.
             * Since the I/O interface is per image we could make this more
             * flexible in the future if we want to. */
            if (fUseNewIo)
            {
#ifdef VBOX_WITH_PDM_ASYNC_COMPLETION
                pImage->VDIfIo.pfnOpen              = drvvdAsyncIOOpen;
                pImage->VDIfIo.pfnClose             = drvvdAsyncIOClose;
                pImage->VDIfIo.pfnGetSize           = drvvdAsyncIOGetSize;
                pImage->VDIfIo.pfnSetSize           = drvvdAsyncIOSetSize;
                pImage->VDIfIo.pfnSetAllocationSize = drvvdAsyncIOSetAllocationSize;
                pImage->VDIfIo.pfnReadSync          = drvvdAsyncIOReadSync;
                pImage->VDIfIo.pfnWriteSync         = drvvdAsyncIOWriteSync;
                pImage->VDIfIo.pfnFlushSync         = drvvdAsyncIOFlushSync;
                pImage->VDIfIo.pfnReadAsync         = drvvdAsyncIOReadAsync;
                pImage->VDIfIo.pfnWriteAsync        = drvvdAsyncIOWriteAsync;
                pImage->VDIfIo.pfnFlushAsync        = drvvdAsyncIOFlushAsync;
#else /* !VBOX_WITH_PDM_ASYNC_COMPLETION */
                rc = PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES,
                                         RT_SRC_POS, N_("DrvVD: Configuration error: Async Completion Framework not compiled in"));
#endif /* !VBOX_WITH_PDM_ASYNC_COMPLETION */
                if (RT_SUCCESS(rc))
                    rc = VDInterfaceAdd(&pImage->VDIfIo.Core, "DrvVD_IO", VDINTERFACETYPE_IO,
                                        pThis, sizeof(VDINTERFACEIO), &pImage->pVDIfsImage);
                AssertRC(rc);
            }

            /*
             * Open the image.
             */
            unsigned uOpenFlags;
            if (fReadOnly || pThis->fTempReadOnly || iLevel != 0)
                uOpenFlags = VD_OPEN_FLAGS_READONLY;
            else
                uOpenFlags = VD_OPEN_FLAGS_NORMAL;
            if (fHonorZeroWrites)
                uOpenFlags |= VD_OPEN_FLAGS_HONOR_ZEROES;
            if (pThis->fAsyncIOSupported)
                uOpenFlags |= VD_OPEN_FLAGS_ASYNC_IO;
            if (pThis->fShareable)
                uOpenFlags |= VD_OPEN_FLAGS_SHAREABLE;
            if (fDiscard && iLevel == 0)
                uOpenFlags |= VD_OPEN_FLAGS_DISCARD;
            if (fInformAboutZeroBlocks)
                uOpenFlags |= VD_OPEN_FLAGS_INFORM_ABOUT_ZERO_BLOCKS;
            if (   (uOpenFlags & VD_OPEN_FLAGS_READONLY)
                && fSkipConsistencyChecks)
                uOpenFlags |= VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS;

            /* Try to open backend in async I/O mode first. */
            rc = VDOpen(pThis->pDisk, pszFormat, pszName, uOpenFlags, pImage->pVDIfsImage);
            if (rc == VERR_NOT_SUPPORTED)
            {
                pThis->fAsyncIOSupported = false;
                uOpenFlags &= ~VD_OPEN_FLAGS_ASYNC_IO;
                rc = VDOpen(pThis->pDisk, pszFormat, pszName, uOpenFlags, pImage->pVDIfsImage);
            }

            if (rc == VERR_VD_DISCARD_NOT_SUPPORTED)
            {
                fDiscard = false;
                uOpenFlags &= ~VD_OPEN_FLAGS_DISCARD;
                rc = VDOpen(pThis->pDisk, pszFormat, pszName, uOpenFlags, pImage->pVDIfsImage);
            }

            if (!fDiscard)
            {
                pThis->IMedia.pfnDiscard           = NULL;
                pThis->IMediaEx.pfnIoReqDiscard    = NULL;
            }

            if (RT_SUCCESS(rc))
            {
                LogFunc(("%d - Opened '%s' in %s mode\n",
                         iLevel, pszName,
                         VDIsReadOnly(pThis->pDisk) ? "read-only" : "read-write"));
                if (  VDIsReadOnly(pThis->pDisk)
                    && !fReadOnly
                    && !fMaybeReadOnly
                    && !pThis->fTempReadOnly
                    && iLevel == 0)
                {
                    rc = PDMDrvHlpVMSetError(pDrvIns, VERR_VD_IMAGE_READ_ONLY, RT_SRC_POS,
                                             N_("Failed to open image '%s' for writing due to wrong permissions"),
                                             pszName);
                    break;
                }
            }
            else
            {
               rc = PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                        N_("Failed to open image '%s' in %s mode"), pszName,
                                        (uOpenFlags & VD_OPEN_FLAGS_READONLY) ? "read-only" : "read-write");
               break;
            }

            PDMDrvHlpMMHeapFree(pDrvIns, pszName);
            pszName = NULL;
            PDMDrvHlpMMHeapFree(pDrvIns, pszFormat);
            pszFormat = NULL;

            /* next */
            iLevel--;
            iImageIdx++;
            pCurNode = pHlp->pfnCFGMGetParent(pCurNode);
        }

        LogRel(("VD: Opening the disk took %lld ns\n", RTTimeNanoTS() - tsStart));

        /* Open the cache image if set. */
        if (   RT_SUCCESS(rc)
            && RT_VALID_PTR(pszCachePath))
        {
            /* Insert the custom I/O interface only if we're told to use new IO.
             * Since the I/O interface is per image we could make this more
             * flexible in the future if we want to. */
            if (fUseNewIo)
            {
#ifdef VBOX_WITH_PDM_ASYNC_COMPLETION
                pThis->VDIfIoCache.pfnOpen       = drvvdAsyncIOOpen;
                pThis->VDIfIoCache.pfnClose      = drvvdAsyncIOClose;
                pThis->VDIfIoCache.pfnGetSize    = drvvdAsyncIOGetSize;
                pThis->VDIfIoCache.pfnSetSize    = drvvdAsyncIOSetSize;
                pThis->VDIfIoCache.pfnReadSync   = drvvdAsyncIOReadSync;
                pThis->VDIfIoCache.pfnWriteSync  = drvvdAsyncIOWriteSync;
                pThis->VDIfIoCache.pfnFlushSync  = drvvdAsyncIOFlushSync;
                pThis->VDIfIoCache.pfnReadAsync  = drvvdAsyncIOReadAsync;
                pThis->VDIfIoCache.pfnWriteAsync = drvvdAsyncIOWriteAsync;
                pThis->VDIfIoCache.pfnFlushAsync = drvvdAsyncIOFlushAsync;
#else /* !VBOX_WITH_PDM_ASYNC_COMPLETION */
                rc = PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES,
                                         RT_SRC_POS, N_("DrvVD: Configuration error: Async Completion Framework not compiled in"));
#endif /* !VBOX_WITH_PDM_ASYNC_COMPLETION */
                if (RT_SUCCESS(rc))
                    rc = VDInterfaceAdd(&pThis->VDIfIoCache.Core, "DrvVD_IO", VDINTERFACETYPE_IO,
                                        pThis, sizeof(VDINTERFACEIO), &pThis->pVDIfsCache);
                AssertRC(rc);
            }

            rc = VDCacheOpen(pThis->pDisk, pszCacheFormat, pszCachePath, VD_OPEN_FLAGS_NORMAL, pThis->pVDIfsCache);
            if (RT_FAILURE(rc))
                rc = PDMDRV_SET_ERROR(pDrvIns, rc, N_("DrvVD: Could not open cache image"));
        }

        if (RT_VALID_PTR(pszCachePath))
            PDMDrvHlpMMHeapFree(pDrvIns, pszCachePath);
        if (RT_VALID_PTR(pszCacheFormat))
            PDMDrvHlpMMHeapFree(pDrvIns, pszCacheFormat);

        if (   RT_SUCCESS(rc)
            && pThis->fMergePending
            && (   pThis->uMergeSource == VD_LAST_IMAGE
                || pThis->uMergeTarget == VD_LAST_IMAGE))
        {
            rc = PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRIVER_INVALID_PROPERTIES,
                                  N_("DrvVD: Configuration error: Inconsistent image merge data"));
        }

        /* Create the block cache if enabled. */
        if (   fUseBlockCache
            && !pThis->fShareable
            && !fDiscard
            && !pThis->CfgCrypto.pCfgNode /* Disk encryption disables the block cache for security reasons */
            && RT_SUCCESS(rc))
        {
            /*
             * We need a unique ID for the block cache (to identify the owner of data
             * blocks in a saved state). UUIDs are not really suitable because
             * there are image formats which don't support them. Furthermore it is
             * possible that a new diff image was attached after a saved state
             * which changes the UUID.
             * However the device "name + device instance + LUN" triple the disk is
             * attached to is always constant for saved states.
             */
            char *pszId = NULL;
            uint32_t iInstance, iLUN;
            const char *pcszController;

            rc = pThis->pDrvMediaPort->pfnQueryDeviceLocation(pThis->pDrvMediaPort, &pcszController,
                                                              &iInstance, &iLUN);
            if (RT_FAILURE(rc))
                rc = PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRIVER_INVALID_PROPERTIES,
                                      N_("DrvVD: Configuration error: Could not query device data"));
            else
            {
                int cbStr = RTStrAPrintf(&pszId, "%s-%d-%d", pcszController, iInstance, iLUN);

                if (cbStr > 0)
                {
                    rc = PDMDrvHlpBlkCacheRetain(pDrvIns, &pThis->pBlkCache,
                                                 drvvdBlkCacheXferCompleteIoReq,
                                                 drvvdBlkCacheXferEnqueue,
                                                 drvvdBlkCacheXferEnqueueDiscard,
                                                 pszId);
                    if (rc == VERR_NOT_SUPPORTED)
                    {
                        LogRel(("VD: Block cache is not supported\n"));
                        rc = VINF_SUCCESS;
                    }
                    else
                        AssertRC(rc);

                    RTStrFree(pszId);
                }
                else
                    rc = PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_DRIVER_INVALID_PROPERTIES,
                                          N_("DrvVD: Out of memory when creating block cache"));
            }
        }

        if (RT_SUCCESS(rc))
            rc = drvvdSetupFilters(pThis, pCfg);

        /*
         * Register a load-done callback so we can undo TempReadOnly config before
         * we get to drvvdResume.  Automatically deregistered upon destruction.
         */
        if (RT_SUCCESS(rc))
            rc = PDMDrvHlpSSMRegisterEx(pDrvIns, 0 /* version */, 0 /* cbGuess */,
                                        NULL /*pfnLivePrep*/, NULL /*pfnLiveExec*/, NULL /*pfnLiveVote*/,
                                        NULL /*pfnSavePrep*/, NULL /*pfnSaveExec*/, NULL /*pfnSaveDone*/,
                                        NULL /*pfnDonePrep*/, NULL /*pfnLoadExec*/, drvvdLoadDone);

        /* Setup the boot acceleration stuff if enabled. */
        if (RT_SUCCESS(rc) && pThis->fBootAccelEnabled)
        {
            pThis->cbDisk = VDGetSize(pThis->pDisk, VD_LAST_IMAGE);
            Assert(pThis->cbDisk > 0);
            pThis->pbData = (uint8_t *)RTMemAllocZ(pThis->cbBootAccelBuffer);
            if (pThis->pbData)
            {
                pThis->fBootAccelActive = true;
                pThis->offDisk          = 0;
                pThis->cbDataValid      = 0;
                LogRel(("VD: Boot acceleration enabled\n"));
            }
            else
                LogRel(("VD: Boot acceleration, out of memory, disabled\n"));
        }

        if (   RTUuidIsNull(&pThis->Uuid)
            && pThis->enmType == PDMMEDIATYPE_HARD_DISK)
            VDGetUuid(pThis->pDisk, 0, &pThis->Uuid);

        /*
         * Automatically upgrade the floppy drive if the specified one is too
         * small to represent the whole boot time image. (We cannot do this later
         * since the BIOS (and others) gets the info via CMOS.)
         *
         * This trick should make 2.88 images as well as the fake 15.6 and 63.5 MB
         * images despite the hardcoded default 1.44 drive.
         */
        if (   PDMMEDIATYPE_IS_FLOPPY(pThis->enmType)
            && pThis->pDisk)
        {
            uint64_t     const cbFloppyImg = VDGetSize(pThis->pDisk, VD_LAST_IMAGE);
            PDMMEDIATYPE const enmCfgType  = pThis->enmType;
            switch (enmCfgType)
            {
                default:
                    AssertFailed();
                    RT_FALL_THRU();
                case PDMMEDIATYPE_FLOPPY_360:
                    if (cbFloppyImg > 40 * 2 * 9 * 512)
                        pThis->enmType = PDMMEDIATYPE_FLOPPY_720;
                    RT_FALL_THRU();
                case PDMMEDIATYPE_FLOPPY_720:
                    if (cbFloppyImg > 80 * 2 * 9 * 512)
                        pThis->enmType = PDMMEDIATYPE_FLOPPY_1_20;
                    RT_FALL_THRU();
                case PDMMEDIATYPE_FLOPPY_1_20:
                    if (cbFloppyImg > 80 * 2 * 15 * 512)
                        pThis->enmType = PDMMEDIATYPE_FLOPPY_1_44;
                    RT_FALL_THRU();
                case PDMMEDIATYPE_FLOPPY_1_44:
                    if (cbFloppyImg > 80 * 2 * 18 * 512)
                        pThis->enmType = PDMMEDIATYPE_FLOPPY_2_88;
                    RT_FALL_THRU();
                case PDMMEDIATYPE_FLOPPY_2_88:
                    if (cbFloppyImg > 80 * 2 * 36 * 512)
                        pThis->enmType = PDMMEDIATYPE_FLOPPY_FAKE_15_6;
                    RT_FALL_THRU();
                case PDMMEDIATYPE_FLOPPY_FAKE_15_6:
                    if (cbFloppyImg > 255 * 2 * 63 * 512)
                        pThis->enmType = PDMMEDIATYPE_FLOPPY_FAKE_63_5;
                    RT_FALL_THRU();
                case PDMMEDIATYPE_FLOPPY_FAKE_63_5:
                    if (cbFloppyImg > 255 * 2 * 255 * 512)
                        LogRel(("Warning: Floppy image is larger that 63.5 MB! (%llu bytes)\n", cbFloppyImg));
                    break;
            }
            if (pThis->enmType != enmCfgType)
                LogRel(("DrvVD: Automatically upgraded floppy drive from %s to %s to better support the %u byte image\n",
                        drvvdGetTypeName(enmCfgType), drvvdGetTypeName(pThis->enmType), cbFloppyImg));
        }
    } /* !fEmptyDrive */

    if (RT_SUCCESS(rc))
        drvvdStatsRegister(pThis);

    if (RT_FAILURE(rc))
    {
        if (RT_VALID_PTR(pszName))
            PDMDrvHlpMMHeapFree(pDrvIns, pszName);
        if (RT_VALID_PTR(pszFormat))
            PDMDrvHlpMMHeapFree(pDrvIns, pszFormat);
        /* drvvdDestruct does the rest. */
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * VBox disk container media driver registration record.
 */
const PDMDRVREG g_DrvVD =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "VD",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Generic VBox disk media driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_MEDIA,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(VBOXDISK),
    /* pfnConstruct */
    drvvdConstruct,
    /* pfnDestruct */
    drvvdDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    drvvdPowerOn,
    /* pfnReset */
    drvvdReset,
    /* pfnSuspend */
    drvvdSuspend,
    /* pfnResume */
    drvvdResume,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    drvvdPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

