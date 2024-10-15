/* $Id: VSCSIInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual SCSI driver: Internal defines
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

#ifndef VBOX_INCLUDED_SRC_Storage_VSCSI_VSCSIInternal_h
#define VBOX_INCLUDED_SRC_Storage_VSCSI_VSCSIInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vscsi.h>
#include <VBox/scsi.h>
#include <VBox/scsiinline.h>
#include <iprt/err.h>
#include <iprt/memcache.h>
#include <iprt/sg.h>
#include <iprt/list.h>

#include "VSCSIVpdPages.h"

/** Pointer to an internal virtual SCSI device. */
typedef VSCSIDEVICEINT        *PVSCSIDEVICEINT;
/** Pointer to an internal virtual SCSI device LUN. */
typedef VSCSILUNINT           *PVSCSILUNINT;
/** Pointer to an internal virtual SCSI device LUN pointer. */
typedef PVSCSILUNINT          *PPVSCSILUNINT;
/** Pointer to a virtual SCSI LUN descriptor. */
typedef struct VSCSILUNDESC   *PVSCSILUNDESC;
/** Pointer to a virtual SCSI request. */
typedef VSCSIREQINT           *PVSCSIREQINT;
/** Pointer to a virtual SCSI I/O request. */
typedef VSCSIIOREQINT         *PVSCSIIOREQINT;
/** Pointer to virtual SCSI sense data state. */
typedef struct VSCSISENSE     *PVSCSISENSE;

/**
 * Virtual SCSI sense data handling.
 */
typedef struct VSCSISENSE
{
    /** Buffer holding the sense data. */
    uint8_t              abSenseBuf[32];
} VSCSISENSE;

/**
 * Virtual SCSI device.
 */
typedef struct VSCSIDEVICEINT
{
    /** Request completion callback */
    PFNVSCSIREQCOMPLETED pfnVScsiReqCompleted;
    /** Opaque user data. */
    void                *pvVScsiDeviceUser;
    /** Number of LUNs currently attached. */
    uint32_t             cLunsAttached;
    /** How many LUNs are fitting in the array. */
    uint32_t             cLunsMax;
    /** Request cache */
    RTMEMCACHE           hCacheReq;
    /** Sense data handling. */
    VSCSISENSE           VScsiSense;
    /** Pointer to the array of LUN handles.
     *  The index is the LUN id. */
    PPVSCSILUNINT        papVScsiLun;
} VSCSIDEVICEINT;

/**
 * Virtual SCSI device LUN.
 */
typedef struct VSCSILUNINT
{
    /** Pointer to the parent SCSI device. */
    PVSCSIDEVICEINT      pVScsiDevice;
    /** Opaque user data */
    void                *pvVScsiLunUser;
    /** I/O callback table */
    PVSCSILUNIOCALLBACKS pVScsiLunIoCallbacks;
    /** Pointer to the LUN type descriptor. */
    PVSCSILUNDESC        pVScsiLunDesc;
    /** Flag indicating whether LUN is ready. */
    bool                 fReady;
    /** Flag indicating media presence in LUN. */
    bool                 fMediaPresent;
    /** Flags of supported features. */
    uint64_t             fFeatures;
    /** I/O request processing data */
    struct
    {
        /** Number of outstanding tasks on this LUN. */
        volatile uint32_t cReqOutstanding;
    } IoReq;
} VSCSILUNINT;

/**
 * Virtual SCSI request.
 */
typedef struct VSCSIREQINT
{
    /** The LUN the request is for. */
    uint32_t             iLun;
    /** The CDB */
    uint8_t             *pbCDB;
    /** Size of the CDB */
    size_t               cbCDB;
    /** S/G buffer. */
    RTSGBUF              SgBuf;
    /** Pointer to the sense buffer. */
    uint8_t             *pbSense;
    /** Size of the sense buffer */
    size_t               cbSense;
    /** Opaque user data associated with this request */
    void                *pvVScsiReqUser;
    /** Transfer size determined from the CDB. */
    size_t               cbXfer;
    /** Number of bytes of sense data written. */
    size_t               cbSenseWritten;
    /** Transfer direction as indicated by the CDB. */
    VSCSIXFERDIR         enmXferDir;
    /** Pointer to the opaque data which may be allocated by the LUN
     * the request is for. */
    void                *pvLun;
} VSCSIREQINT;

/**
 * Virtual SCSI I/O request.
 */
typedef struct VSCSIIOREQINT
{
    /** The associated request. */
    PVSCSIREQINT           pVScsiReq;
    /** Lun for this I/O request. */
    PVSCSILUNINT           pVScsiLun;
    /** Transfer direction */
    VSCSIIOREQTXDIR        enmTxDir;
    /** Direction dependent data. */
    union
    {
        /** Read/Write request. */
        struct
        {
            /** Start offset */
            uint64_t       uOffset;
            /** Number of bytes to transfer */
            size_t         cbTransfer;
            /** Number of bytes the S/G list holds */
            size_t         cbSeg;
            /** Number of segments. */
            unsigned       cSeg;
            /** Segment array. */
            PCRTSGSEG      paSeg;
        } Io;
        /** Unmap request. */
        struct
        {
            /** Array of ranges to unmap. */
            PRTRANGE       paRanges;
            /** Number of ranges. */
            unsigned       cRanges;
        } Unmap;
    } u;
} VSCSIIOREQINT;

/**
 * VPD page pool.
 */
typedef struct VSCSIVPDPOOL
{
    /** List of registered pages (VSCSIVPDPAGE). */
    RTLISTANCHOR    ListPages;
} VSCSIVPDPOOL;
/** Pointer to the VSCSI VPD page pool. */
typedef VSCSIVPDPOOL *PVSCSIVPDPOOL;

/**
 * Supported operation code information entry.
 */
typedef struct VSCSILUNSUPOPC
{
    /** The operation code. */
    uint8_t                 u8Opc;
    /** Service action code if required as indicated by
     * VSCSI_LUN_SUP_OPC_SVC_ACTION_REQUIRED */
    uint16_t                u16SvcAction;
    /** Flags. */
    uint32_t                fFlags;
    /** Readable description for the op code. */
    const char              *pszOpc;
    /** The length of the CDB for this operation code. */
    uint8_t                 cbCdb;
    /** Pointer to the CDB usage data. */
    uint8_t                 *pbCdbUsage;
    /* The operation specific valuefor the timeout descriptor. */
    uint8_t                 u8OpcTimeoutSpec;
    /** The nominal processing timeout in seconds. */
    uint16_t                cNominalProcessingTimeout;
    /** The recommend timeout in seconds. */
    uint16_t                cRecommendTimeout;
} VSCSILUNSUPOPC;
/** Pointer to a operation code information entry. */
typedef VSCSILUNSUPOPC *PVSCSILUNSUPOPC;
/** Pointer to a const operation code information entry. */
typedef const VSCSILUNSUPOPC *PCVSCSILUNSUPOPC;

/** @name Flags for the supported operation code infromation entries.
 * @{ */
/** Flag indicating wheter the service action member is valid and should be
 * evaluated to find the desired opcode information. */
#define VSCSI_LUN_SUP_OPC_SVC_ACTION_REQUIRED      RT_BIT_32(0)
/** Flag whether the values for the timeout descriptor are valid. */
#define VSCSI_LUN_SUP_OPC_TIMEOUT_DESC_VALID       RT_BIT_32(1)
/** @} */

/** @name Support macros to create supported operation code information entries.
 * @{ */
#define VSCSI_LUN_SUP_OPC(a_u8Opc, a_pszOpc, a_cbCdb, a_pbCdbUsage) \
    { a_u8Opc, 0, 0, a_pszOpc, a_cbCdb, a_pbCdbUsage, 0, 0, 0}
#define VSCSI_LUN_SUP_OPC_SVC(a_u8Opc, a_u16SvcAction, a_pszOpc, a_cbCdb, a_pbCdbUsage) \
    { a_u8Opc, a_u16SvcAction, VSCSI_LUN_SUP_OPC_SVC_ACTION_REQUIRED, a_pszOpc, a_cbCdb, a_pbCdbUsage, 0, 0, 0}
/** @} */

/** @name Helper macros to specify a range of not supported CDB opcodes for the entries pointed
 * to by VSCSILUNDESC::pacbCdbOpc.
 * @{ */
#define VSCSI_LUN_CDB_SZ_INVALID 0
#define VSCSI_LUN_CDB_SZ_INVALID_X2 \
    VSCSI_LUN_CDB_SZ_INVALID, \
    VSCSI_LUN_CDB_SZ_INVALID
#define VSCSI_LUN_CDB_SZ_INVALID_X4 \
    VSCSI_LUN_CDB_SZ_INVALID_X2, \
    VSCSI_LUN_CDB_SZ_INVALID_X2
#define VSCSI_LUN_CDB_SZ_INVALID_X8 \
    VSCSI_LUN_CDB_SZ_INVALID_X4, \
    VSCSI_LUN_CDB_SZ_INVALID_X4
#define VSCSI_LUN_CDB_SZ_INVALID_X16 \
    VSCSI_LUN_CDB_SZ_INVALID_X8, \
    VSCSI_LUN_CDB_SZ_INVALID_X8
/** @} */


/**
 * Virtual SCSI LUN descriptor.
 */
typedef struct VSCSILUNDESC
{
    /** Device type this descriptor emulates. */
    VSCSILUNTYPE         enmLunType;
    /** Descriptor name */
    const char          *pcszDescName;
    /** LUN type size */
    size_t               cbLun;
    /** Pointer to the array holding the CDB length indexed by the opcode.
     * A 0 entry means opcode not supported (CDB must be at least 1 byte big). */
    const uint8_t       *pacbCdbOpc;
    /** Number of entries in the supported operation codes array. */
    uint32_t             cSupOpcInfo;
    /** Pointer to the array of supported operation codes for the
     * REPORT RUPPORTED OPERATION CODES command handled by the generic
     * device driver - optional.
     */
    PCVSCSILUNSUPOPC     paSupOpcInfo;

    /**
     * Initialise a Lun instance.
     *
     * @returns VBox status code.
     * @param   pVScsiLun    The SCSI LUN instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunInit, (PVSCSILUNINT pVScsiLun));

    /**
     * Destroy a Lun instance.
     *
     * @returns VBox status code.
     * @param   pVScsiLun    The SCSI LUN instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunDestroy, (PVSCSILUNINT pVScsiLun));

    /**
     * Processes a SCSI request.
     *
     * @returns VBox status code.
     * @param   pVScsiLun    The SCSI LUN instance.
     * @param   pVScsiReq    The SCSi request to process.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunReqProcess, (PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq));

    /**
     * Frees additional allocated resources for the given request if it was allocated before.
     *
     * @returns void.
     * @param   pVScsiLun    The SCSI LUN instance.
     * @param   pVScsiReq    The SCSI request.
     * @param   pvScsiReqLun The opaque data allocated previously.
     */
    DECLR3CALLBACKMEMBER(void, pfnVScsiLunReqFree, (PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq,
                                                    void *pvScsiReqLun));

    /**
     * Informs about a medium being inserted - optional.
     *
     * @returns VBox status code.
     * @param   pVScsiLun    The SCSI LUN instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunMediumInserted, (PVSCSILUNINT pVScsiLun));

    /**
     * Informs about a medium being removed - optional.
     *
     * @returns VBox status code.
     * @param   pVScsiLun    The SCSI LUN instance.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunMediumRemoved, (PVSCSILUNINT pVScsiLun));

} VSCSILUNDESC;

/** Maximum number of LUNs a device can have. */
#define VSCSI_DEVICE_LUN_MAX 128

/**
 * Completes a SCSI request and calls the completion handler.
 *
 * @param   pVScsiDevice    The virtual SCSI device.
 * @param   pVScsiReq       The request which completed.
 * @param   rcScsiCode      The status code
 *                          One of the SCSI_STATUS_* #defines.
 * @param   fRedoPossible   Flag whether redo is possible.
 * @param   rcReq           Informational return code of the request.
 */
void vscsiDeviceReqComplete(PVSCSIDEVICEINT pVScsiDevice, PVSCSIREQINT pVScsiReq,
                            int rcScsiCode, bool fRedoPossible, int rcReq);

/**
 * Init the sense data state.
 *
 * @param   pVScsiSense  The SCSI sense data state to init.
 */
void vscsiSenseInit(PVSCSISENSE pVScsiSense);

/**
 * Sets a ok sense code.
 *
 * @returns SCSI status code.
 * @param   pVScsiSense  The SCSI sense state to use.
 * @param   pVScsiReq    The SCSI request.
 */
int vscsiReqSenseOkSet(PVSCSISENSE pVScsiSense, PVSCSIREQINT pVScsiReq);

/**
 * Sets an error sense code.
 *
 * @returns SCSI status code.
 * @param   pVScsiSense   The SCSI sense state to use.
 * @param   pVScsiReq     The SCSI request.
 * @param   uSCSISenseKey The SCSI sense key to set.
 * @param   uSCSIASC      The ASC value.
 * @param   uSCSIASC      The ASCQ value.
 */
int vscsiReqSenseErrorSet(PVSCSISENSE pVScsiSense, PVSCSIREQINT pVScsiReq, uint8_t uSCSISenseKey,
                          uint8_t uSCSIASC, uint8_t uSCSIASCQ);

/**
 * Sets an error sense code with additional information.
 *
 * @returns SCSI status code.
 * @param   pVScsiSense   The SCSI sense state to use.
 * @param   pVScsiReq     The SCSI request.
 * @param   uSCSISenseKey The SCSI sense key to set.
 * @param   uSCSIASC      The ASC value.
 * @param   uSCSIASC      The ASCQ value.
 * @param   uInfo         The 32-bit sense information.
 */
int vscsiReqSenseErrorInfoSet(PVSCSISENSE pVScsiSense, PVSCSIREQINT pVScsiReq, uint8_t uSCSISenseKey,
                              uint8_t uSCSIASC, uint8_t uSCSIASCQ, uint32_t uInfo);

/**
 * Process a request sense command.
 *
 * @returns SCSI status code.
 * @param   pVScsiSense   The SCSI sense state to use.
 * @param   pVScsiReq     The SCSI request.
 */
int vscsiReqSenseCmd(PVSCSISENSE pVScsiSense, PVSCSIREQINT pVScsiReq);

/**
 * Inits the VPD page pool.
 *
 * @returns VBox status code.
 * @param   pVScsiVpdPool    The VPD page pool to initialize.
 */
int vscsiVpdPagePoolInit(PVSCSIVPDPOOL pVScsiVpdPool);

/**
 * Destroys the given VPD page pool freeing all pages in it.
 *
 * @param   pVScsiVpdPool    The VPD page pool to destroy.
 */
void vscsiVpdPagePoolDestroy(PVSCSIVPDPOOL pVScsiVpdPool);

/**
 * Allocates a new page in the VPD page pool with the given number.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXIST if the page number is in use.
 * @param   pVScsiVpdPool    The VPD page pool the page will belong to.
 * @param   uPage            The page number, must be unique.
 * @param   cbPage           Size of the page in bytes.
 * @param   ppbPage          Where to store the pointer to the raw page data on success.
 */
int vscsiVpdPagePoolAllocNewPage(PVSCSIVPDPOOL pVScsiVpdPool, uint8_t uPage, size_t cbPage, uint8_t **ppbPage);

/**
 * Queries the given page from the pool and cpies it to the buffer given
 * by the SCSI request.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the page is not in the pool.
 * @param   pVScsiVpdPool    The VPD page pool to use.
 * @param   pVScsiReq        The SCSI request.
 * @param   uPage            Page to query.
 */
int vscsiVpdPagePoolQueryPage(PVSCSIVPDPOOL pVScsiVpdPool, PVSCSIREQINT pVScsiReq, uint8_t uPage);

/**
 * Inits the I/O request related state for the LUN.
 *
 * @returns VBox status code.
 * @param   pVScsiLun    The LUN instance.
 */
int vscsiIoReqInit(PVSCSILUNINT pVScsiLun);

/**
 * Enqueues a new flush request
 *
 * @returns VBox status code.
 * @param   pVScsiLun    The LUN instance which issued the request.
 * @param   pVScsiReq    The virtual SCSI request associated with the flush.
 */
int vscsiIoReqFlushEnqueue(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq);

/**
 * Enqueue a new data transfer request.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN instance which issued the request.
 * @param   pVScsiReq   The virtual SCSI request associated with the transfer.
 * @param   enmTxDir    Transfer direction.
 * @param   uOffset     Start offset of the transfer.
 * @param   cbTransfer  Number of bytes to transfer.
 */
int vscsiIoReqTransferEnqueue(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq,
                              VSCSIIOREQTXDIR enmTxDir, uint64_t uOffset,
                              size_t cbTransfer);

/**
 * Enqueue a new data transfer request - extended variant.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN instance which issued the request.
 * @param   pVScsiReq   The virtual SCSI request associated with the transfer.
 * @param   enmTxDir    Transfer direction.
 * @param   uOffset     Start offset of the transfer.
 * @param   paSegs      Pointer to the array holding the memory buffer segments.
 * @param   cSegs       Number of segments in the array.
 * @param   cbTransfer  Number of bytes to transfer.
 */
int vscsiIoReqTransferEnqueueEx(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq,
                                VSCSIIOREQTXDIR enmTxDir, uint64_t uOffset,
                                PCRTSGSEG paSegs, unsigned cSegs, size_t cbTransfer);

/**
 * Enqueue a new unmap request.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN instance which issued the request.
 * @param   pVScsiReq   The virtual SCSI request associated with the transfer.
 * @param   paRanges    The array of ranges to unmap.
 * @param   cRanges     Number of ranges in the array.
 */
int vscsiIoReqUnmapEnqueue(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq,
                           PRTRANGE paRanges, unsigned cRanges);

/**
 * Returns the current number of outstanding tasks on the given LUN.
 *
 * @returns Number of outstanding tasks.
 * @param   pVScsiLun   The LUN to check.
 */
uint32_t vscsiIoReqOutstandingCountGet(PVSCSILUNINT pVScsiLun);

/**
 * Sets the transfer size for the given request.
 *
 * @param   pVScsiReq     The SCSI request.
 * @param   cbXfer        The transfer size for the request.
 */
DECLINLINE(void) vscsiReqSetXferSize(PVSCSIREQINT pVScsiReq, size_t cbXfer)
{
    pVScsiReq->cbXfer = cbXfer;
}

/**
 * Sets the transfer direction for the given request.
 *
 * @param   pVScsiReq     The SCSI request.
 * @param   cbXfer        The transfer size for the request.
 */
DECLINLINE(void) vscsiReqSetXferDir(PVSCSIREQINT pVScsiReq, VSCSIXFERDIR enmXferDir)
{
    pVScsiReq->enmXferDir = enmXferDir;
}

/**
 * Wrapper for the set I/O request allocation size I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun             The LUN.
 * @param   cbVScsiIoReqAlloc     The additional size for the request to allocate.
 */
DECLINLINE(int) vscsiLunReqAllocSizeSet(PVSCSILUNINT pVScsiLun, size_t cbVScsiIoReqAlloc)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunReqAllocSizeSet(pVScsiLun,
                                                                       pVScsiLun->pvVScsiLunUser,
                                                                       cbVScsiIoReqAlloc);
}

/**
 * Wrapper for the allocate I/O request I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun             The LUN.
 * @param   u64Tag                A unique tag to assign to the request.
 * @param   ppVScsiIoReq          Where to store the pointer to the request on success.
 */
DECLINLINE(int) vscsiLunReqAlloc(PVSCSILUNINT pVScsiLun, uint64_t u64Tag, PVSCSIIOREQINT *ppVScsiIoReq)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunReqAlloc(pVScsiLun,
                                                                pVScsiLun->pvVScsiLunUser,
                                                                u64Tag, ppVScsiIoReq);
}

/**
 * Wrapper for the free I/O request I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN.
 * @param   pVScsiIoReq The request to free.
 */
DECLINLINE(int) vscsiLunReqFree(PVSCSILUNINT pVScsiLun, PVSCSIIOREQINT pVScsiIoReq)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunReqFree(pVScsiLun,
                                                               pVScsiLun->pvVScsiLunUser,
                                                               pVScsiIoReq);
}

/**
 * Wrapper for the get medium region count I/O callback.
 *
 * @returns Number of regions for the underlying medium.
 * @param   pVScsiLun   The LUN.
 */
DECLINLINE(uint32_t) vscsiLunMediumGetRegionCount(PVSCSILUNINT pVScsiLun)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunMediumGetRegionCount(pVScsiLun,
                                                                            pVScsiLun->pvVScsiLunUser);
}

/**
 * Wrapper for the query medium region properties I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun     The LUN.
 * @param   uRegion       The region index to query the properties of.
 * @param   pu64LbaStart  Where to store the starting LBA for the region on success.
 * @param   pcBlocks      Where to store the number of blocks for the region on success.
 * @param   pcbBlock      Where to store the size of one block in bytes on success.
 * @param   penmDataForm  WHere to store the data form for the region on success.
 */
DECLINLINE(int) vscsiLunMediumQueryRegionProperties(PVSCSILUNINT pVScsiLun, uint32_t uRegion,
                                                    uint64_t *pu64LbaStart, uint64_t *pcBlocks,
                                                    uint64_t *pcbBlock, PVDREGIONDATAFORM penmDataForm)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunMediumQueryRegionProperties(pVScsiLun,
                                                                                   pVScsiLun->pvVScsiLunUser,
                                                                                   uRegion, pu64LbaStart,
                                                                                   pcBlocks, pcbBlock,
                                                                                   penmDataForm);
}

/**
 * Wrapper for the query medium region properties for LBA I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun     The LUN.
 * @param   uRegion       The region index to query the properties of.
 * @param   pu64LbaStart  Where to store the starting LBA for the region on success.
 * @param   pcBlocks      Where to store the number of blocks for the region on success.
 * @param   pcbBlock      Where to store the size of one block in bytes on success.
 * @param   penmDataForm  WHere to store the data form for the region on success.
 */
DECLINLINE(int) vscsiLunMediumQueryRegionPropertiesForLba(PVSCSILUNINT pVScsiLun, uint64_t u64LbaStart, uint32_t *puRegion,
                                                          uint64_t *pcBlocks, uint64_t *pcbBlock,
                                                          PVDREGIONDATAFORM penmDataForm)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunMediumQueryRegionPropertiesForLba(pVScsiLun,
                                                                                         pVScsiLun->pvVScsiLunUser,
                                                                                         u64LbaStart, puRegion,
                                                                                         pcBlocks, pcbBlock,
                                                                                         penmDataForm);
}

/**
 * Wrapper for the get medium lock/unlock I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN.
 * @param   bool        The new medium lock state.
 */
DECLINLINE(int) vscsiLunMediumSetLock(PVSCSILUNINT pVScsiLun, bool fLocked)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunMediumSetLock(pVScsiLun,
                                                                     pVScsiLun->pvVScsiLunUser,
                                                                     fLocked);
}

/**
 * Wrapper for the eject medium I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN.
 */
DECLINLINE(int) vscsiLunMediumEject(PVSCSILUNINT pVScsiLun)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunMediumEject(pVScsiLun,
                                                                   pVScsiLun->pvVScsiLunUser);
}

/**
 * Wrapper for the I/O request enqueue I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN.
 * @param   pVScsiIoReq The I/O request to enqueue.
 */
DECLINLINE(int) vscsiLunReqTransferEnqueue(PVSCSILUNINT pVScsiLun, PVSCSIIOREQINT pVScsiIoReq)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunReqTransferEnqueue(pVScsiLun,
                                                                          pVScsiLun->pvVScsiLunUser,
                                                                          pVScsiIoReq);
}

/**
 * Wrapper for the get feature flags I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN.
 * @param   pfFeatures  Where to sthre supported flags on success.
 */
DECLINLINE(int) vscsiLunGetFeatureFlags(PVSCSILUNINT pVScsiLun, uint64_t *pfFeatures)
{
    return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunGetFeatureFlags(pVScsiLun,
                                                                       pVScsiLun->pvVScsiLunUser,
                                                                       pfFeatures);
}

/**
 * Wrapper for the query INQUIRY strings I/O callback.
 *
 * @returns VBox status code.
 * @param   pVScsiLun   The LUN.
 * @param   ppszVendorId     Where to store the pointer to the vendor ID string to report.
 * @param   ppszProductId    Where to store the pointer to the product ID string to report.
 * @param   ppszProductLevel Where to store the pointer to the revision string to report.
 */
DECLINLINE(int) vscsiLunQueryInqStrings(PVSCSILUNINT pVScsiLun, const char **ppszVendorId,
                                        const char **ppszProductId, const char **ppszProductLevel)
{
    if (pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunQueryInqStrings)
    {
        return pVScsiLun->pVScsiLunIoCallbacks->pfnVScsiLunQueryInqStrings(pVScsiLun,
                                                                           pVScsiLun->pvVScsiLunUser,
                                                                           ppszVendorId, ppszProductId,
                                                                           ppszProductLevel);
    }

    return VERR_NOT_FOUND;
}

/**
 * Wrapper around vscsiReqSenseOkSet()
 */
DECLINLINE(int) vscsiLunReqSenseOkSet(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq)
{
    return vscsiReqSenseOkSet(&pVScsiLun->pVScsiDevice->VScsiSense, pVScsiReq);
}

/**
 * Wrapper around vscsiReqSenseErrorSet()
 */
DECLINLINE(int) vscsiLunReqSenseErrorSet(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq, uint8_t uSCSISenseKey, uint8_t uSCSIASC, uint8_t uSCSIASCQ)
{
    return vscsiReqSenseErrorSet(&pVScsiLun->pVScsiDevice->VScsiSense, pVScsiReq, uSCSISenseKey, uSCSIASC, uSCSIASCQ);
}

/**
 * Wrapper around vscsiReqSenseErrorInfoSet()
 */
DECLINLINE(int) vscsiLunReqSenseErrorInfoSet(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq, uint8_t uSCSISenseKey, uint8_t uSCSIASC, uint8_t uSCSIASCQ, uint32_t uInfo)
{
    return vscsiReqSenseErrorInfoSet(&pVScsiLun->pVScsiDevice->VScsiSense, pVScsiReq, uSCSISenseKey, uSCSIASC, uSCSIASCQ, uInfo);
}

#endif /* !VBOX_INCLUDED_SRC_Storage_VSCSI_VSCSIInternal_h */

