/* $Id: dvm.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Disk Volume Management Internals.
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

#ifndef IPRT_INCLUDED_INTERNAL_dvm_h
#define IPRT_INCLUDED_INTERNAL_dvm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/vfs.h>
#include "internal/magics.h"

RT_C_DECLS_BEGIN

/** Format specific volume manager handle. */
typedef struct RTDVMFMTINTERNAL *RTDVMFMT;
/** Pointer to a format specific volume manager handle. */
typedef RTDVMFMT                *PRTDVMFMT;
/** NIL volume manager handle. */
#define NIL_RTDVMFMT             ((RTDVMFMT)~0)

/** Format specific volume data handle. */
typedef struct RTDVMVOLUMEFMTINTERNAL *RTDVMVOLUMEFMT;
/** Pointer to a format specific volume data handle. */
typedef RTDVMVOLUMEFMT                *PRTDVMVOLUMEFMT;
/** NIL volume handle. */
#define NIL_RTDVMVOLUMEFMT             ((RTDVMVOLUMEFMT)~0)

/**
 * Disk descriptor.
 */
typedef struct RTDVMDISK
{
    /** Size of the disk in bytes. */
    uint64_t        cbDisk;
    /** Sector size. */
    uint64_t        cbSector;
    /** The VFS file handle if backed by such. */
    RTVFSFILE       hVfsFile;
} RTDVMDISK;
/** Pointer to a disk descriptor. */
typedef RTDVMDISK *PRTDVMDISK;
/** Pointer to a const descriptor. */
typedef const RTDVMDISK *PCRTDVMDISK;

/** Score to indicate that the backend can't handle the format at all */
#define RTDVM_MATCH_SCORE_UNSUPPORTED 0
/** Score to indicate that a backend supports the format
 * but there can be other backends. */
#define RTDVM_MATCH_SCORE_SUPPORTED   (UINT32_MAX/2)
/** Score to indicate a perfect match. */
#define RTDVM_MATCH_SCORE_PERFECT     UINT32_MAX

/**
 * Volume format operations.
 */
typedef struct RTDVMFMTOPS
{
    /** Name of the format. */
    const char         *pszFmt;
    /** The format type.   */
    RTDVMFORMATTYPE     enmFormat;

    /**
     * Probes the given disk for known structures.
     *
     * @returns IPRT status code.
     * @param   pDisk           Disk descriptor.
     * @param   puScore         Where to store the match score on success.
     */
    DECLCALLBACKMEMBER(int, pfnProbe,(PCRTDVMDISK pDisk, uint32_t *puScore));

    /**
     * Opens the format to set up all structures.
     *
     * @returns IPRT status code.
     * @param   pDisk           The disk descriptor.
     * @param   phVolMgrFmt     Where to store the volume format data on success.
     */
    DECLCALLBACKMEMBER(int, pfnOpen,(PCRTDVMDISK pDisk, PRTDVMFMT phVolMgrFmt));

    /**
     * Initializes a new volume map.
     *
     * @returns IPRT status code.
     * @param   pDisk           The disk descriptor.
     * @param   phVolMgrFmt     Where to store the volume format data on success.
     */
    DECLCALLBACKMEMBER(int, pfnInitialize,(PCRTDVMDISK pDisk, PRTDVMFMT phVolMgrFmt));

    /**
     * Closes the volume format.
     *
     * @param   hVolMgrFmt      The format specific volume manager handle.
     */
    DECLCALLBACKMEMBER(void, pfnClose,(RTDVMFMT hVolMgrFmt));

    /**
     * Returns whether the given range is in use by the volume manager.
     *
     * @returns IPRT status code.
     * @param   hVolMgrFmt      The format specific volume manager handle.
     * @param   offStart        Start offset of the range.
     * @param   cbRange         Size of the range to check in bytes.
     * @param   pfUsed          Where to store whether the range is in use by the
     *                          volume manager.
     */
    DECLCALLBACKMEMBER(int, pfnQueryRangeUse,(RTDVMFMT hVolMgrFmt,
                                              uint64_t off, uint64_t cbRange,
                                              bool *pfUsed));

    /**
     * Optional: Query the uuid of the current disk if applicable.
     *
     * @returns IPRT status code.
     * @retval  VERR_NOT_SUPPORTED if the partition scheme doesn't do UUIDs.
     * @param   hVolMgrFmt      The format specific volume manager handle.
     * @param   pUuid           Where to return the UUID.
     */
    DECLCALLBACKMEMBER(int, pfnQueryDiskUuid,(RTDVMFMT hVolMgrFmt, PRTUUID pUuid));

    /**
     * Gets the number of valid volumes in the map.
     *
     * @returns Number of valid volumes in the map or UINT32_MAX on failure.
     * @param   hVolMgrFmt      The format specific volume manager handle.
     */
    DECLCALLBACKMEMBER(uint32_t, pfnGetValidVolumes,(RTDVMFMT hVolMgrFmt));

    /**
     * Gets the maximum number of volumes the map can have.
     *
     * @returns Maximum number of volumes in the map or 0 on failure.
     * @param   hVolMgrFmt      The format specific volume manager handle.
     */
    DECLCALLBACKMEMBER(uint32_t, pfnGetMaxVolumes,(RTDVMFMT hVolMgrFmt));

    /**
     * Get the first valid volume from a map.
     *
     * @returns IPRT status code.
     * @param   hVolMgrFmt      The format specific volume manager handle.
     * @param   phVolFmt        Where to store the volume handle to the first volume
     *                          on success.
     */
    DECLCALLBACKMEMBER(int, pfnQueryFirstVolume,(RTDVMFMT hVolMgrFmt, PRTDVMVOLUMEFMT phVolFmt));

    /**
     * Get the first valid volume from a map.
     *
     * @returns IPRT status code.
     * @param   hVolMgrFmt      The format specific volume manager handle.
     * @param   hVolFmt         The current volume.
     * @param   phVolFmtNext    Where to store the handle to the format specific
     *                          volume data of the next volume on success.
     */
    DECLCALLBACKMEMBER(int, pfnQueryNextVolume,(RTDVMFMT hVolMgrFmt, RTDVMVOLUMEFMT hVolFmt, PRTDVMVOLUMEFMT phVolFmtNext));

    /**
     * Query the partition table locations.
     *
     * @returns IPRT status code.
     * @retval  VERR_BUFFER_OVERFLOW if the table is too small, @a *pcActual will be
     *          set to the required size.
     * @retval  VERR_BUFFER_UNDERFLOW if the table is too big and @a pcActual is
     *          NULL.
     * @param   hVolMgrFmt      The format specific volume manager handle.
     * @param   fFlags          Flags, see RTDVMMAPQTABLOC_F_XXX.
     * @param   paLocations     Where to return the info. Ignored if @a cLocations
     *                          is zero, then only @a pcActual matters.
     * @param   cLocations      The size of @a paLocations in items.
     * @param   pcActual        Where to return the actual number of locations, or
     *                          on VERR_BUFFER_OVERFLOW the necessary table size.
     *                          Optional, when not specified the cLocations value
     *                          must match exactly or it fails with
     *                          VERR_BUFFER_UNDERFLOW.
     * @sa RTDvmMapQueryTableLocations
     */
    DECLCALLBACKMEMBER(int, pfnQueryTableLocations,(RTDVMFMT hVolMgrFmt, uint32_t fFlags, PRTDVMTABLELOCATION paLocations,
                                                    size_t cLocations, size_t *pcActual));

    /**
     * Closes a volume handle.
     *
     * @param   hVolFmt         The format specific volume handle.
     */
    DECLCALLBACKMEMBER(void, pfnVolumeClose,(RTDVMVOLUMEFMT hVolFmt));

    /**
     * Gets the size of the given volume.
     *
     * @returns Size of the volume in bytes or 0 on failure.
     * @param   hVolFmt         The format specific volume handle.
     */
    DECLCALLBACKMEMBER(uint64_t, pfnVolumeGetSize,(RTDVMVOLUMEFMT hVolFmt));

    /**
     * Queries the name of the given volume.
     *
     * @returns IPRT status code.
     * @param   hVolFmt         The format specific volume handle.
     * @param   ppszVolname     Where to store the name of the volume on success.
     */
    DECLCALLBACKMEMBER(int, pfnVolumeQueryName,(RTDVMVOLUMEFMT hVolFmt, char **ppszVolName));

    /**
     * Get the type of the given volume.
     *
     * @returns The volume type on success, DVMVOLTYPE_INVALID if hVol is invalid.
     * @param   hVolFmt         The format specific volume handle.
     */
    DECLCALLBACKMEMBER(RTDVMVOLTYPE, pfnVolumeGetType,(RTDVMVOLUMEFMT hVolFmt));

    /**
     * Get the flags of the given volume.
     *
     * @returns The volume flags or UINT64_MAX on failure.
     * @param   hVolFmt         The format specific volume handle.
     */
    DECLCALLBACKMEMBER(uint64_t, pfnVolumeGetFlags,(RTDVMVOLUMEFMT hVolFmt));

    /**
     * Queries the range of the given volume on the underyling medium.
     *
     * @returns IPRT status code.
     * @param   hVolFmt         The format specific volume handle.
     * @param   poffStart       Where to store the start byte offset on the
     *                          underlying medium.
     * @param   poffLast        Where to store the last byte offset on the
     *                          underlying medium (inclusive).
     */
    DECLCALLBACKMEMBER(int, pfnVolumeQueryRange,(RTDVMVOLUMEFMT hVolFmt, uint64_t *poffStart, uint64_t *poffLast));

    /**
     * Returns whether the supplied range is at least partially intersecting
     * with the given volume.
     *
     * @returns whether the range intersects with the volume.
     * @param   hVolFmt         The format specific volume handle.
     * @param   offStart        Start offset of the range.
     * @param   cbRange         Size of the range to check in bytes.
     * @param   poffVol         Where to store the offset of the range from the
     *                          start of the volume if true is returned.
     * @param   pcbIntersect    Where to store the number of bytes intersecting
     *                          with the range if true is returned.
     */
    DECLCALLBACKMEMBER(bool, pfnVolumeIsRangeIntersecting,(RTDVMVOLUMEFMT hVolFmt,
                                                           uint64_t offStart, size_t cbRange,
                                                           uint64_t *poffVol,
                                                           uint64_t *pcbIntersect));

    /**
     * Queries the range of the partition table the volume belongs to on the underlying medium.
     *
     * @returns IPRT status code.
     * @param   hVolFmt         The format specific volume handle.
     * @param   poffTable       Where to return the byte offset on the underlying
     *                          media of the (partition/volume/whatever) table.
     * @param   pcbTable        Where to return the table size in bytes.  This
     *                          typically includes alignment padding.
     * @sa RTDvmVolumeQueryTableLocation
     */
    DECLCALLBACKMEMBER(int, pfnVolumeQueryTableLocation,(RTDVMVOLUMEFMT hVolFmt, uint64_t *poffStart, uint64_t *poffLast));

    /**
     * Gets the tiven index for the specified volume.
     *
     * @returns The requested index. UINT32_MAX on failure.
     * @param   hVolFmt         The format specific volume handle.
     * @param   enmIndex        The index to get. Never RTDVMVOLIDX_HOST.
     * @sa RTDvmVolumeGetIndex
     */
    DECLCALLBACKMEMBER(uint32_t, pfnVolumeGetIndex,(RTDVMVOLUMEFMT hVolFmt, RTDVMVOLIDX enmIndex));

    /**
     * Query a generic volume property.
     *
     * This is an extensible interface for retriving mostly format specific
     * information, or information that's not commonly used.  (It's modelled after
     * RTLdrQueryPropEx.)
     *
     * @returns IPRT status code.
     * @retval  VERR_NOT_SUPPORTED if the property query isn't supported (either all
     *          or that specific property).  The caller  must  handle this result.
     * @retval  VERR_NOT_FOUND is currently not returned, but intended for cases
     *          where it wasn't present in the tables.
     * @retval  VERR_INVALID_FUNCTION if the @a enmProperty value is wrong.
     * @retval  VERR_INVALID_PARAMETER if the fixed buffer size is wrong. Correct
     *          size in @a *pcbBuf.
     * @retval  VERR_BUFFER_OVERFLOW if the property doesn't have a fixed size
     *          buffer and the buffer isn't big enough. Correct size in @a *pcbBuf.
     * @retval  VERR_INVALID_HANDLE if the handle is invalid.
     *
     * @param   hVolFmt     Handle to the volume.
     * @param   enmProperty The property to query.
     * @param   pvBuf       Pointer to the input / output buffer.  In most cases
     *                      it's only used for returning data.
     * @param   cbBuf       The size of the buffer.  This is validated by the common
     *                      code for all fixed typed & sized properties.  The
     *                      interger properties may have several supported sizes, in
     *                      which case the user value is passed along as-is but it
     *                      is okay to return a smaller amount of data.  The common
     *                      code will make upcast the data.
     * @param   pcbBuf      Where to return the amount of data returned.  This must
     *                      be set even for fixed type/sized data.
     * @sa RTDvmVolumeQueryProp, RTDvmVolumeGetPropU64
     */
    DECLCALLBACKMEMBER(int, pfnVolumeQueryProp,(RTDVMVOLUMEFMT hVolFmt, RTDVMVOLPROP enmProperty,
                                                void *pvBuf, size_t cbBuf, size_t *pcbBuf));

    /**
     * Read data from the given volume.
     *
     * @returns IPRT status code.
     * @param   hVolFmt         The format specific volume handle.
     * @param   off             Where to start reading from.
     * @param   pvBuf           Where to store the read data.
     * @param   cbRead          How many bytes to read.
     */
    DECLCALLBACKMEMBER(int, pfnVolumeRead,(RTDVMVOLUMEFMT hVolFmt, uint64_t off, void *pvBuf, size_t cbRead));

    /**
     * Write data to the given volume.
     *
     * @returns IPRT status code.
     * @param   hVolFmt         The format specific volume handle.
     * @param   off             Where to start writing to.
     * @param   pvBuf           The data to write.
     * @param   cbWrite         How many bytes to write.
     */
    DECLCALLBACKMEMBER(int, pfnVolumeWrite,(RTDVMVOLUMEFMT hVolFmt, uint64_t off, const void *pvBuf, size_t cbWrite));

} RTDVMFMTOPS;
/** Pointer to a DVM ops table. */
typedef RTDVMFMTOPS *PRTDVMFMTOPS;
/** Pointer to a const DVM ops table. */
typedef const RTDVMFMTOPS *PCRTDVMFMTOPS;

/** Checks whether a range is intersecting. */
#define RTDVM_RANGE_IS_INTERSECTING(start, size, off) ( (start) <= (off) && ((start) + (size)) > (off) )

/** Converts a LBA number to the byte offset. */
#define RTDVM_LBA2BYTE(lba, disk) ((lba) * (disk)->cbSector)
/** Converts a Byte offset to the LBA number. */
#define RTDVM_BYTE2LBA(off, disk) ((off) / (disk)->cbSector)

/**
 * Returns the number of sectors in the disk.
 *
 * @returns Number of sectors.
 * @param   pDisk   The disk descriptor.
 */
DECLINLINE(uint64_t) rtDvmDiskGetSectors(PCRTDVMDISK pDisk)
{
    return pDisk->cbDisk / pDisk->cbSector;
}

/**
 * Read from the disk at the given offset.
 *
 * @returns IPRT status code.
 * @param   pDisk    The disk descriptor to read from.
 * @param   off      Start offset.
 * @param   pvBuf    Destination buffer.
 * @param   cbRead   How much to read.
 * @sa      rtDvmDiskReadUnaligned
 */
DECLINLINE(int) rtDvmDiskRead(PCRTDVMDISK pDisk, uint64_t off, void *pvBuf, size_t cbRead)
{
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbRead > 0, VERR_INVALID_PARAMETER);
    AssertReturn(off + cbRead <= pDisk->cbDisk, VERR_INVALID_PARAMETER);

    /* Use RTVfsFileReadAt if these triggers: */
    Assert(!(cbRead % pDisk->cbSector));
    Assert(!(off    % pDisk->cbSector));

    return RTVfsFileReadAt(pDisk->hVfsFile, off, pvBuf, cbRead, NULL /*pcbRead*/);
}

DECLHIDDEN(int) rtDvmDiskReadUnaligned(PCRTDVMDISK pDisk, uint64_t off, void *pvBuf, size_t cbRead);

/**
 * Write to the disk at the given offset.
 *
 * @returns IPRT status code.
 * @param   pDisk    The disk descriptor to write to.
 * @param   off      Start offset.
 * @param   pvBuf    Source buffer.
 * @param   cbWrite  How much to write.
 */
DECLINLINE(int) rtDvmDiskWrite(PCRTDVMDISK pDisk, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    AssertPtrReturn(pDisk, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbWrite > 0, VERR_INVALID_PARAMETER);
    AssertReturn(off + cbWrite <= pDisk->cbDisk, VERR_INVALID_PARAMETER);

    /* Write RTVfsFileReadAt if these triggers: */
    Assert(!(cbWrite % pDisk->cbSector));
    Assert(!(off     % pDisk->cbSector));

    return RTVfsFileWriteAt(pDisk->hVfsFile, off, pvBuf, cbWrite, NULL /*pcbWritten*/);
}

extern DECL_HIDDEN_DATA(const RTDVMFMTOPS) g_rtDvmFmtMbr;
extern DECL_HIDDEN_DATA(const RTDVMFMTOPS) g_rtDvmFmtGpt;
extern DECL_HIDDEN_DATA(const RTDVMFMTOPS) g_rtDvmFmtBsdLbl;

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_dvm_h */

