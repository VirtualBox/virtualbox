/* $Id: vscsi.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage drivers - Virtual SCSI driver
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

#ifndef VBOX_INCLUDED_vscsi_h
#define VBOX_INCLUDED_vscsi_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vdmedia.h>
#include <iprt/sg.h>

RT_C_DECLS_BEGIN

#ifdef IN_RING0
# error "There are no VBox VSCSI APIs available in Ring-0 Host Context!"
#endif

/** @defgroup grp_drv_vscsi  Virtual VSCSI Driver
 * @ingroup grp_devdrv
 * @{
 */
/** @todo figure better grouping.   */

/** A virtual SCSI device handle */
typedef struct VSCSIDEVICEINT *VSCSIDEVICE;
/** A pointer to a virtual SCSI device handle. */
typedef VSCSIDEVICE           *PVSCSIDEVICE;
/** A virtual SCSI LUN handle. */
typedef struct VSCSILUNINT    *VSCSILUN;
/** A pointer to a virtual SCSI LUN handle. */
typedef VSCSILUN              *PVSCSILUN;
/** A virtual SCSI request handle. */
typedef struct VSCSIREQINT    *VSCSIREQ;
/** A pointer to a virtual SCSI request handle. */
typedef VSCSIREQ              *PVSCSIREQ;
/** A SCSI I/O request handle. */
typedef struct VSCSIIOREQINT  *VSCSIIOREQ;
/** A pointer to a SCSI I/O request handle. */
typedef VSCSIIOREQ            *PVSCSIIOREQ;

/**
 * Virtual SCSI I/O request transfer direction.
 */
typedef enum VSCSIIOREQTXDIR
{
    /** Invalid direction */
    VSCSIIOREQTXDIR_INVALID = 0,
    /** Read */
    VSCSIIOREQTXDIR_READ,
    /** Write */
    VSCSIIOREQTXDIR_WRITE,
    /** Flush */
    VSCSIIOREQTXDIR_FLUSH,
    /** Unmap */
    VSCSIIOREQTXDIR_UNMAP,
    /** 32bit hack */
    VSCSIIOREQTXDIR_32BIT_HACK = 0x7fffffff
} VSCSIIOREQTXDIR;
/** Pointer to a SCSI LUN type */
typedef VSCSIIOREQTXDIR *PVSCSIIOREQTXDIR;

/**
 * Virtual SCSI transfer direction as seen from the initiator.
 */
typedef enum VSCSIXFERDIR
{
    /** Invalid data direction. */
    PVSCSIXFERDIR_INVALID     = 0,
    /** Direction is unknown. */
    VSCSIXFERDIR_UNKNOWN,
    /** Direction is from target to initiator (aka a read). */
    VSCSIXFERDIR_T2I,
    /** Direction is from initiator to device (aka a write). */
    VSCSIXFERDIR_I2T,
    /** No data transfer associated with this request. */
    VSCSIXFERDIR_NONE,
    /** 32bit hack. */
    VSCSIXFERDIR_32BIT_HACK  = 0x7fffffff
} VSCSIXFERDIR;

/**
 * LUN types we support
 */
typedef enum VSCSILUNTYPE
{
    /** Invalid type */
    VSCSILUNTYPE_INVALID = 0,
    /** Hard disk (SBC) */
    VSCSILUNTYPE_SBC,
    /** CD/DVD drive (MMC) */
    VSCSILUNTYPE_MMC,
    /** Tape drive (SSC) */
    VSCSILUNTYPE_SSC,
    /** Last value to indicate an invalid device */
    VSCSILUNTYPE_LAST,
    /** 32bit hack */
    VSCSILUNTYPE_32BIT_HACK = 0x7fffffff
} VSCSILUNTYPE;
/** Pointer to a SCSI LUN type */
typedef VSCSILUNTYPE *PVSCSILUNTYPE;

/** The LUN can handle the UNMAP command. */
#define VSCSI_LUN_FEATURE_UNMAP          RT_BIT(0)
/** The LUN has a non rotational medium. */
#define VSCSI_LUN_FEATURE_NON_ROTATIONAL RT_BIT(1)
/** The medium of the LUN is readonly. */
#define VSCSI_LUN_FEATURE_READONLY       RT_BIT(2)

/**
 * Virtual SCSI LUN I/O Callback table.
 */
typedef struct VSCSILUNIOCALLBACKS
{
    /**
     * Sets the size of the allocator specific memory for a I/O request.
     *
     * @returns VBox status code.
     * @param   hVScsiLun            Virtual SCSI LUN handle.
     * @param   pvScsiLunUser        Opaque user data which may be used to identify the
     *                               medium.
     * @param   cbVScsiIoReqAlloc    The size of the allocator specific memory in bytes.
     * @thread  EMT.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunReqAllocSizeSet, (VSCSILUN hVScsiLun, void *pvScsiLunUser,
                                                           size_t cbVScsiIoReqAlloc));

    /**
     * Allocates a new I/O request.
     *
     * @returns VBox status code.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     * @param   u64Tag          A tag to assign to the request handle for identification later on.
     * @param   phVScsiIoReq    Where to store the handle to the allocated I/O request on success.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunReqAlloc, (VSCSILUN hVScsiLun, void *pvScsiLunUser,
                                                    uint64_t u64Tag, PVSCSIIOREQ phVScsiIoReq));

    /**
     * Frees a given I/O request.
     *
     * @returns VBox status code.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     * @param   hVScsiIoReq     The VSCSI I/O request to free.
     * @thread  Any thread.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunReqFree, (VSCSILUN hVScsiLun, void *pvScsiLunUser, VSCSIIOREQ hVScsiIoReq));

    /**
     * Returns the number of regions for the medium.
     *
     * @returns Number of regions.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnVScsiLunMediumGetRegionCount,(VSCSILUN hVScsiLun, void *pvScsiLunUser));

    /**
     * Queries the properties for the given region.
     *
     * @returns VBox status code.
     * @retval  VERR_NOT_FOUND if the region index is not known.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     * @param   uRegion         The region index to query the properties of.
     * @param   pu64LbaStart    Where to store the starting LBA for the region on success.
     * @param   pcBlocks        Where to store the number of blocks for the region on success.
     * @param   pcbBlock        Where to store the size of one block in bytes on success.
     * @param   penmDataForm    WHere to store the data form for the region on success.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunMediumQueryRegionProperties,(VSCSILUN hVScsiLun, void *pvScsiLunUser,
                                                                      uint32_t uRegion, uint64_t *pu64LbaStart,
                                                                      uint64_t *pcBlocks, uint64_t *pcbBlock,
                                                                      PVDREGIONDATAFORM penmDataForm));

    /**
     * Queries the properties for the region covering the given LBA.
     *
     * @returns VBox status code.
     * @retval  VERR_NOT_FOUND if the region index is not known.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     * @param   u64LbaStart     Where to store the starting LBA for the region on success.
     * @param   puRegion        Where to store the region number on success.
     * @param   pcBlocks        Where to store the number of blocks left in this region starting from the given LBA.
     * @param   pcbBlock        Where to store the size of one block in bytes on success.
     * @param   penmDataForm    WHere to store the data form for the region on success.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunMediumQueryRegionPropertiesForLba,(VSCSILUN hVScsiLun, void *pvScsiLunUser,
                                                                            uint64_t u64LbaStart, uint32_t *puRegion,
                                                                            uint64_t *pcBlocks, uint64_t *pcbBlock,
                                                                            PVDREGIONDATAFORM penmDataForm));

    /**
     * Set the lock state of the underlying medium.
     *
     * @returns VBox status status code.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     * @param   fLocked         New lock state (locked/unlocked).
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunMediumSetLock,(VSCSILUN hVScsiLun, void *pvScsiLunUser, bool fLocked));

    /**
     * Eject the attached medium.
     *
     * @returns VBox status code.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunMediumEject, (VSCSILUN hVScsiLun, void *pvScsiLunUser));

    /**
     * Enqueue a read or write request from the medium.
     *
     * @returns VBox status status code.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     * @param   hVScsiIoReq     Virtual SCSI I/O request handle.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunReqTransferEnqueue,(VSCSILUN hVScsiLun, void *pvScsiLunUser, VSCSIIOREQ hVScsiIoReq));

    /**
     * Returns flags of supported features.
     *
     * @returns VBox status status code.
     * @param   hVScsiLun       Virtual SCSI LUN handle.
     * @param   pvScsiLunUser   Opaque user data which may be used to identify the
     *                          medium.
     * @param   pfFeatures      Where to return the queried features.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunGetFeatureFlags,(VSCSILUN hVScsiLun, void *pvScsiLunUser, uint64_t *pfFeatures));

    /**
     * Queries the vendor and product ID and revision to report for INQUIRY commands of the given LUN.
     *
     * @returns VBox status status code.
     * @retval  VERR_NOT_FOUND if the data is not available and some defaults should be sued instead.
     * @param   hVScsiLun        Virtual SCSI LUN handle.
     * @param   pvScsiLunUser    Opaque user data which may be used to identify the
     *                           medium.
     * @param   ppszVendorId     Where to store the pointer to the vendor ID string to report.
     * @param   ppszProductId    Where to store the pointer to the product ID string to report.
     * @param   ppszProductLevel Where to store the pointer to the product level string to report.
     */
    DECLR3CALLBACKMEMBER(int, pfnVScsiLunQueryInqStrings, (VSCSILUN hVScsiLun, void *pvScsiLunUser, const char **ppszVendorId,
                                                           const char **ppszProductId, const char **ppszProductLevel));

} VSCSILUNIOCALLBACKS;
/** Pointer to a virtual SCSI LUN I/O callback table. */
typedef VSCSILUNIOCALLBACKS *PVSCSILUNIOCALLBACKS;

/**
 * The virtual SCSI request completed callback.
 */
typedef DECLCALLBACKTYPE(void, FNVSCSIREQCOMPLETED,(VSCSIDEVICE hVScsiDevice,
                                                    void *pvVScsiDeviceUser,
                                                    void *pvVScsiReqUser,
                                                    int rcScsiCode,
                                                    bool fRedoPossible,
                                                    int rcReq,
                                                    size_t cbXfer,
                                                    VSCSIXFERDIR enmXferDir,
                                                    size_t cbSense));
/** Pointer to a virtual SCSI request completed callback. */
typedef FNVSCSIREQCOMPLETED *PFNVSCSIREQCOMPLETED;

/**
 * Create a new empty SCSI device instance.
 *
 * @returns VBox status code.
 * @param   phVScsiDevice           Where to store the SCSI device handle.
 * @param   pfnVScsiReqCompleted    The method call after a request completed.
 * @param   pvVScsiDeviceUser       Opaque user data given in the completion callback.
 */
VBOXDDU_DECL(int) VSCSIDeviceCreate(PVSCSIDEVICE phVScsiDevice,
                                    PFNVSCSIREQCOMPLETED pfnVScsiReqCompleted,
                                    void *pvVScsiDeviceUser);

/**
 * Destroy a SCSI device instance.
 *
 * @returns VBox status code.
 * @param   hVScsiDevice   The SCSI device handle to destroy.
 */
VBOXDDU_DECL(int) VSCSIDeviceDestroy(VSCSIDEVICE hVScsiDevice);

/**
 * Attach a LUN to the SCSI device.
 *
 * @returns VBox status code.
 * @param   hVScsiDevice   The SCSI device handle to add the LUN to.
 * @param   hVScsiLun      The LUN handle to add.
 * @param   iLun           The LUN number.
 */
VBOXDDU_DECL(int) VSCSIDeviceLunAttach(VSCSIDEVICE hVScsiDevice, VSCSILUN hVScsiLun, uint32_t iLun);

/**
 * Detach a LUN from the SCSI device.
 *
 * @returns VBox status code.
 * @param   hVScsiDevice    The SCSI device handle to add the LUN to.
 * @param   iLun            The LUN number to remove.
 * @param   phVScsiLun      Where to store the detached LUN handle.
 */
VBOXDDU_DECL(int) VSCSIDeviceLunDetach(VSCSIDEVICE hVScsiDevice, uint32_t iLun,
                                       PVSCSILUN phVScsiLun);

/**
 * Query the SCSI LUN type.
 *
 * @returns VBox status code.
 * @param   hVScsiDevice    The SCSI device handle.
 * @param   iLun            The LUN number to get.
 * @param   pEnmLunType     Where to store the LUN type.
 */
VBOXDDU_DECL(int) VSCSIDeviceLunQueryType(VSCSIDEVICE hVScsiDevice, uint32_t iLun,
                                          PVSCSILUNTYPE pEnmLunType);

/**
 * Enqueue a request to the SCSI device.
 *
 * @returns VBox status code.
 * @param   hVScsiDevice    The SCSI device handle.
 * @param   hVScsiReq       The SCSI request handle to enqueue.
 */
VBOXDDU_DECL(int) VSCSIDeviceReqEnqueue(VSCSIDEVICE hVScsiDevice, VSCSIREQ hVScsiReq);

/**
 * Allocate a new request handle.
 *
 * @returns VBox status code.
 * @param   hVScsiDevice      The SCSI device handle.
 * @param   phVScsiReq        Where to SCSI request handle.
 * @param   iLun              The LUN the request is for.
 * @param   pbCDB             The CDB for the request.
 * @param   cbCDB             The size of the CDB in bytes.
 * @param   cbSGList          Number of bytes the S/G list describes.
 * @param   cSGListEntries    Number of S/G list entries.
 * @param   paSGList          Pointer to the S/G list.
 * @param   pbSense           Pointer to the sense buffer.
 * @param   cbSense           Size of the sense buffer.
 * @param   pvVScsiReqUser    Opqaue user data returned when the request completes.
 */
VBOXDDU_DECL(int) VSCSIDeviceReqCreate(VSCSIDEVICE hVScsiDevice, PVSCSIREQ phVScsiReq,
                                       uint32_t iLun, uint8_t *pbCDB, size_t cbCDB,
                                       size_t cbSGList, unsigned cSGListEntries,
                                       PCRTSGSEG paSGList, uint8_t *pbSense,
                                       size_t cbSense, void *pvVScsiReqUser);

/**
 * Create a new LUN.
 *
 * @returns VBox status code.
 * @param   phVScsiLun              Where to store the SCSI LUN handle.
 * @param   enmLunType              The Lun type.
 * @param   pVScsiLunIoCallbacks    Pointer to the I/O callbacks to use for his LUN.
 * @param   pvVScsiLunUser          Opaque user argument which
 *                                  is returned in the pvScsiLunUser parameter
 *                                  when the request completion callback is called.
 */
VBOXDDU_DECL(int) VSCSILunCreate(PVSCSILUN phVScsiLun, VSCSILUNTYPE enmLunType,
                                 PVSCSILUNIOCALLBACKS pVScsiLunIoCallbacks,
                                 void *pvVScsiLunUser);

/**
 * Destroy virtual SCSI LUN.
 *
 * @returns VBox status code.
 * @param   hVScsiLun               The virtual SCSI LUN handle to destroy.
 */
VBOXDDU_DECL(int) VSCSILunDestroy(VSCSILUN hVScsiLun);

/**
 * Notify virtual SCSI LUN of medium being mounted.
 *
 * @returns VBox status code.
 * @param   hVScsiLun               The virtual SCSI LUN handle to destroy.
 */
VBOXDDU_DECL(int) VSCSILunMountNotify(VSCSILUN hVScsiLun);

/**
 * Notify virtual SCSI LUN of medium being unmounted.
 *
 * @returns VBox status code.
 * @param   hVScsiLun               The virtual SCSI LUN handle to destroy.
 */
VBOXDDU_DECL(int) VSCSILunUnmountNotify(VSCSILUN hVScsiLun);

/**
 * Notify a that a I/O request completed.
 *
 * @returns VBox status code.
 * @param   hVScsiIoReq             The I/O request handle that completed.
 *                                  This is given when a I/O callback for
 *                                  the LUN is called by the virtual SCSI layer.
 * @param   rcIoReq                 The status code the I/O request completed with.
 * @param   fRedoPossible           Flag whether it is possible to redo the request.
 *                                  If true setting any sense code will be omitted
 *                                  in case of an error to not alter the device state.
 */
VBOXDDU_DECL(int) VSCSIIoReqCompleted(VSCSIIOREQ hVScsiIoReq, int rcIoReq, bool fRedoPossible);

/**
 * Query the transfer direction of the I/O request.
 *
 * @returns Transfer direction.of the given I/O request
 * @param   hVScsiIoReq    The SCSI I/O request handle.
 */
VBOXDDU_DECL(VSCSIIOREQTXDIR) VSCSIIoReqTxDirGet(VSCSIIOREQ hVScsiIoReq);

/**
 * Query I/O parameters.
 *
 * @returns VBox status code.
 * @param   hVScsiIoReq    The SCSI I/O request handle.
 * @param   puOffset       Where to store the start offset.
 * @param   pcbTransfer    Where to store the amount of bytes to transfer.
 * @param   pcSeg          Where to store the number of segments in the S/G list.
 * @param   pcbSeg         Where to store the number of bytes the S/G list describes.
 * @param   ppaSeg         Where to store the pointer to the S/G list.
 */
VBOXDDU_DECL(int) VSCSIIoReqParamsGet(VSCSIIOREQ hVScsiIoReq, uint64_t *puOffset,
                                      size_t *pcbTransfer, unsigned *pcSeg,
                                      size_t *pcbSeg, PCRTSGSEG *ppaSeg);

/**
 * Query unmap parameters.
 *
 * @returns VBox status code.
 * @param   hVScsiIoReq    The SCSI I/O request handle.
 * @param   ppaRanges      Where to store the pointer to the range array on success.
 * @param   pcRanges       Where to store the number of ranges on success.
 */
VBOXDDU_DECL(int) VSCSIIoReqUnmapParamsGet(VSCSIIOREQ hVScsiIoReq, PCRTRANGE *ppaRanges,
                                           unsigned *pcRanges);

/** @}  */
RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_vscsi_h */

