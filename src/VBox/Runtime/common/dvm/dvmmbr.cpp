/* $Id: dvmmbr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Disk Volume Management API (DVM) - MBR format backend.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_FS
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/dvm.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include "internal/dvm.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Checks if the partition type is an extended partition container. */
#define RTDVMMBR_IS_EXTENDED(a_bType) ((a_bType) == 0x05 || (a_bType) == 0x0f)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a MBR sector. */
typedef struct RTDVMMBRSECTOR *PRTDVMMBRSECTOR;


/** The on-disk Cylinder/Head/Sector (CHS) info. */
typedef struct MBRCHSADDR
{
    uint8_t uHead;
    uint8_t uSector : 6;
    uint8_t uCylinderH : 2;
    uint8_t uCylinderL;
} MBRCHSADDR;
AssertCompileSize(MBRCHSADDR, 3);


/** A decoded cylinder/head/sector address. */
typedef struct RTDVMMBRCHSADDR
{
    uint16_t uCylinder;
    uint8_t  uHead;
    uint8_t  uSector;
} RTDVMMBRCHSADDR;


/**
 * MBR entry.
 */
typedef struct RTDVMMBRENTRY
{
    /** Our entry in the in-use partition entry list (RTDVMMBRENTRY). */
    RTLISTNODE          ListEntry;
    /** Pointer to the MBR sector containing this entry. */
    PRTDVMMBRSECTOR     pSector;
    /** Pointer to the next sector in the extended partition table chain. */
    PRTDVMMBRSECTOR     pChain;
    /** The byte offset of the start of the partition (relative to disk). */
    uint64_t            offPart;
    /** Number of bytes for this partition. */
    uint64_t            cbPart;
    /** The partition/filesystem type. */
    uint8_t             bType;
    /** The partition flags. */
    uint8_t             fFlags;
    /** Bad entry. */
    bool                fBad;
    /** RTDVMVOLIDX_IN_TABLE - Zero-based index within the table in pSector.
     * (Also the index into RTDVMMBRSECTOR::aEntries.) */
    uint8_t             idxTable;
    /** RTDVMVOLIDX_ALL - One-based index.  All primary entries are included,
     *  whether they are used or not.  In the extended table chain, only USED
     *  entries are counted (but we include RTDVMMBR_IS_EXTENDED entries). */
    uint8_t             idxAll;
    /** RTDVMVOLIDX_USER_VISIBLE - One-base index.  Skips all unused entries
     *  and RTDVMMBR_IS_EXTENDED. */
    uint8_t             idxVisible;
    /** RTDVMVOLIDX_LINUX - One-based index following the /dev/sdaX scheme. */
    uint8_t             idxLinux;
    uint8_t             bUnused;
    /** The first CHS address of this partition */
    RTDVMMBRCHSADDR     FirstChs;
    /** The last CHS address of this partition */
    RTDVMMBRCHSADDR     LastChs;
} RTDVMMBRENTRY;
/** Pointer to an MBR entry. */
typedef RTDVMMBRENTRY *PRTDVMMBRENTRY;

/**
 * A MBR sector.
 */
typedef struct RTDVMMBRSECTOR
{
    /** Internal representation of the entries. */
    RTDVMMBRENTRY       aEntries[4];
    /** The byte offset of this MBR sector (relative to disk).
     * We keep this for detecting cycles now, but it will be needed if we start
     * updating the partition table at some point. */
    uint64_t            offOnDisk;
    /** Pointer to the previous sector if this isn't a primary one. */
    PRTDVMMBRENTRY      pPrevSector;
    /** Set if this is the primary MBR, cleared if an extended. */
    bool                fIsPrimary;
    /** Number of used entries. */
    uint8_t             cUsed;
    /** Number of extended entries. */
    uint8_t             cExtended;
    /** The extended entry we're following (we only follow one, except when
     *  fIsPrimary is @c true). UINT8_MAX if none. */
    uint8_t             idxExtended;
#if ARCH_BITS == 64
    uint32_t            uAlignmentPadding;
#endif
    /** The raw data. */
    uint8_t             abData[RT_FLEXIBLE_ARRAY_NESTED];
} RTDVMMBRSECTOR;

/**
 * MBR volume manager data.
 */
typedef struct RTDVMFMTINTERNAL
{
    /** Pointer to the underlying disk. */
    PCRTDVMDISK         pDisk;
    /** Head of the list of in-use RTDVMMBRENTRY structures.  This excludes
     *  extended partition table entries. */
    RTLISTANCHOR        PartitionHead;
    /** The sector size to use when doing address calculation based on partition
     *  table sector addresses and counts. */
    uint32_t            cbSector;
    /** The total number of partitions, not counting extended ones. */
    uint32_t            cPartitions;
    /** The actual primary MBR sector. */
    RTDVMMBRSECTOR      Primary;
} RTDVMFMTINTERNAL;
/** Pointer to the MBR volume manager. */
typedef RTDVMFMTINTERNAL *PRTDVMFMTINTERNAL;

/**
 * MBR volume data.
 */
typedef struct RTDVMVOLUMEFMTINTERNAL
{
    /** Pointer to the volume manager. */
    PRTDVMFMTINTERNAL   pVolMgr;
    /** The MBR entry.    */
    PRTDVMMBRENTRY      pEntry;
} RTDVMVOLUMEFMTINTERNAL;
/** Pointer to an MBR volume. */
typedef RTDVMVOLUMEFMTINTERNAL *PRTDVMVOLUMEFMTINTERNAL;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Mapping of FS types to DVM volume types.
 *
 * @see https://en.wikipedia.org/wiki/Partition_type
 * @see http://www.win.tue.nl/~aeb/partitions/partition_types-1.html
 */
static const struct RTDVMMBRFS2VOLTYPE
{
    /** MBR FS Id. */
    uint8_t             bFsId;
    /** DVM volume type. */
    RTDVMVOLTYPE        enmVolType;
} g_aFs2DvmVolTypes[] =
{
    { 0x01, RTDVMVOLTYPE_FAT12 },
    { 0x04, RTDVMVOLTYPE_FAT16 },
    { 0x06, RTDVMVOLTYPE_FAT16 }, /* big FAT16 */
    { 0x07, RTDVMVOLTYPE_NTFS }, /* Simplification: Used for HPFS, exFAT, ++, too but NTFS is the more common one. */
    { 0x0b, RTDVMVOLTYPE_FAT32 },
    { 0x0c, RTDVMVOLTYPE_FAT32 },
    { 0x0e, RTDVMVOLTYPE_FAT16 },

    /* Hidden variants of the above: */
    { 0x11, RTDVMVOLTYPE_FAT12 },
    { 0x14, RTDVMVOLTYPE_FAT16 },
    { 0x16, RTDVMVOLTYPE_FAT16 },
    { 0x17, RTDVMVOLTYPE_NTFS },
    { 0x1b, RTDVMVOLTYPE_FAT32 },
    { 0x1c, RTDVMVOLTYPE_FAT32 },
    { 0x1e, RTDVMVOLTYPE_FAT16 },

    { 0x82, RTDVMVOLTYPE_LINUX_SWAP },
    { 0x83, RTDVMVOLTYPE_LINUX_NATIVE },
    { 0x8e, RTDVMVOLTYPE_LINUX_LVM },
    { 0xa5, RTDVMVOLTYPE_FREEBSD },
    { 0xa9, RTDVMVOLTYPE_NETBSD },
    { 0xa6, RTDVMVOLTYPE_OPENBSD },
    { 0xaf, RTDVMVOLTYPE_DARWIN_HFS },
    { 0xbf, RTDVMVOLTYPE_SOLARIS },
    { 0xfd, RTDVMVOLTYPE_LINUX_SOFTRAID }
};


static DECLCALLBACK(int) rtDvmFmtMbrProbe(PCRTDVMDISK pDisk, uint32_t *puScore)
{
    int rc = VINF_SUCCESS;
    *puScore = RTDVM_MATCH_SCORE_UNSUPPORTED;
    if (pDisk->cbDisk > RT_MAX(512, pDisk->cbSector))
    {
        /* Read from the disk and check for the 0x55aa signature at the end. */
        size_t cbAlignedSize = RT_MAX(512, pDisk->cbSector);
        uint8_t *pbMbr = (uint8_t *)RTMemTmpAllocZ(cbAlignedSize);
        if (pbMbr)
        {
            rc = rtDvmDiskRead(pDisk, 0, pbMbr, cbAlignedSize);
            if (   RT_SUCCESS(rc)
                && pbMbr[510] == 0x55
                && pbMbr[511] == 0xaa)
                *puScore = RTDVM_MATCH_SCORE_SUPPORTED; /* Not perfect because GPTs have a protective MBR. */
            /** @todo this could easily confuser a DOS, OS/2 or NT boot sector with a MBR... */
            RTMemTmpFree(pbMbr);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }

    return rc;
}


static void rtDvmFmtMbrDestroy(PRTDVMFMTINTERNAL pThis)
{
    /*
     * Delete chains of extended partitions.
     */
    for (unsigned i = 0; i < 4; i++)
    {
        PRTDVMMBRSECTOR pCur = pThis->Primary.aEntries[i].pChain;
        while (pCur)
        {
            PRTDVMMBRSECTOR pNext = pCur->idxExtended != UINT8_MAX ? pCur->aEntries[pCur->idxExtended].pChain : NULL;

            RT_ZERO(pCur->aEntries);
            pCur->pPrevSector = NULL;
            RTMemFree(pCur);

            pCur = pNext;
        }
    }

    /*
     * Now kill this.
     */
    pThis->pDisk = NULL;
    RT_ZERO(pThis->Primary.aEntries);
    RTMemFree(pThis);
}


/**
 * Decodes the on-disk cylinder/head/sector info and stores it the
 * destination structure.
 */
DECLINLINE(void) rtDvmFmtMbrDecodeChs(RTDVMMBRCHSADDR *pDst, uint8_t *pbRaw)
{
    MBRCHSADDR *pRawChs = (MBRCHSADDR *)pbRaw;
    pDst->uCylinder = RT_MAKE_U16(pRawChs->uCylinderL, pRawChs->uCylinderH);
    pDst->uSector   = pRawChs->uSector;
    pDst->uHead     = pRawChs->uHead;
}


static int rtDvmFmtMbrReadExtended(PRTDVMFMTINTERNAL pThis, PRTDVMMBRENTRY pPrimaryEntry,
                                   uint8_t *pidxAll, uint8_t *pidxVisible, uint8_t *pidxLinux)
{
    uint64_t const  cbExt       = pPrimaryEntry->cbPart;
    uint64_t const  offExtBegin = pPrimaryEntry->offPart;

    uint64_t        offCurBegin = offExtBegin;
    PRTDVMMBRENTRY  pCurEntry   = pPrimaryEntry;
    for (unsigned cTables = 1; ; cTables++)
    {
        /*
         * Do some sanity checking.
         */
        /* Check the address of the partition table. */
        if (offCurBegin - offExtBegin >= cbExt)
        {
            LogRel(("rtDvmFmtMbrReadExtended: offCurBegin=%#RX64 is outside the extended partition: %#RX64..%#RX64 (LB %#RX64)\n",
                    offCurBegin, offExtBegin, offExtBegin + cbExt - 1, cbExt));
            pCurEntry->fBad = true;
            return -VERR_OUT_OF_RANGE;
        }

        /* Limit the chain length. */
        if (cTables > 64)
        {
            LogRel(("rtDvmFmtMbrReadExtended: offCurBegin=%#RX64 is the %uth table, we stop here.\n", offCurBegin, cTables));
            pCurEntry->fBad = true;
            return -VERR_TOO_MANY_SYMLINKS;
        }

        /* Check for obvious cycles. */
        for (PRTDVMMBRENTRY pPrev = pCurEntry->pSector->pPrevSector; pPrev != NULL; pPrev = pPrev->pSector->pPrevSector)
            if (pPrev->offPart == offCurBegin)
            {
                LogRel(("rtDvmFmtMbrReadExtended: Cycle! We've seen offCurBegin=%#RX64 before\n", offCurBegin));
                pCurEntry->fBad = true;
                return -VERR_TOO_MANY_SYMLINKS;
            }

        /*
         * Allocate a new sector entry and read the sector with the table.
         */
        size_t const    cbMbr = RT_MAX(512, pThis->pDisk->cbSector);
        PRTDVMMBRSECTOR pNext = (PRTDVMMBRSECTOR)RTMemAllocZVar(RT_UOFFSETOF_DYN(RTDVMMBRSECTOR, abData[cbMbr]));
        if (!pNext)
            return VERR_NO_MEMORY;
        pNext->offOnDisk    = offCurBegin;
        pNext->pPrevSector  = pCurEntry;
        //pNext->fIsPrimary = false;
        //pNext->cUsed      = 0;
        //pNext->cExtended  = 0;
        pNext->idxExtended  = UINT8_MAX;

        uint8_t *pabData = &pNext->abData[0];
        int rc = rtDvmDiskReadUnaligned(pThis->pDisk, pNext->offOnDisk, pabData, cbMbr);
        if (   RT_FAILURE(rc)
            || pabData[510] != 0x55
            || pabData[511] != 0xaa)
        {
            if (RT_FAILURE(rc))
                LogRel(("rtDvmFmtMbrReadExtended: Error reading extended partition table at sector %#RX64: %Rrc\n", offCurBegin, rc));
            else
                LogRel(("rtDvmFmtMbrReadExtended: Extended partition table at sector %#RX64 does not have a valid DOS signature: %#x %#x\n",
                        offCurBegin, pabData[510], pabData[511]));
            RTMemFree(pNext);
            pCurEntry->fBad = true;
            return rc;
        }
        pCurEntry->pChain = pNext;

        /*
         * Process the table, taking down the first forward entry.
         *
         * As noted in the caller of this function, we only deal with one extended
         * partition entry at this level since noone really ever put more than one
         * here anyway.
         */
        PRTDVMMBRENTRY pEntry     = &pNext->aEntries[0];
        uint8_t       *pbMbrEntry = &pabData[446];
        for (unsigned i = 0; i < 4; i++, pEntry++, pbMbrEntry += 16)
        {
            pEntry->pSector  = pNext;
            pEntry->idxTable = (uint8_t)i;
            RTListInit(&pEntry->ListEntry);

            uint8_t const bType  = pbMbrEntry[4];
            if (bType != 0)
            {
                pEntry->bType    = bType;
                pEntry->fFlags   = pbMbrEntry[0];
                pEntry->idxAll   = *pidxAll;
                *pidxAll += 1;

                rtDvmFmtMbrDecodeChs(&pEntry->FirstChs, &pbMbrEntry[1]);
                rtDvmFmtMbrDecodeChs(&pEntry->LastChs,  &pbMbrEntry[5]);

                pEntry->offPart  = RT_MAKE_U32_FROM_U8(pbMbrEntry[0x08 + 0],
                                                       pbMbrEntry[0x08 + 1],
                                                       pbMbrEntry[0x08 + 2],
                                                       pbMbrEntry[0x08 + 3]);
                pEntry->offPart *= pThis->cbSector;
                pEntry->cbPart   = RT_MAKE_U32_FROM_U8(pbMbrEntry[0x0c + 0],
                                                       pbMbrEntry[0x0c + 1],
                                                       pbMbrEntry[0x0c + 2],
                                                       pbMbrEntry[0x0c + 3]);
                pEntry->cbPart  *= pThis->cbSector;
                if (!RTDVMMBR_IS_EXTENDED(bType))
                {
                    pEntry->offPart    += offCurBegin;
                    pEntry->idxVisible  = *pidxVisible;
                    *pidxVisible += 1;
                    pEntry->idxLinux    = *pidxLinux;
                    *pidxLinux += 1;

                    pThis->cPartitions++;
                    RTListAppend(&pThis->PartitionHead, &pEntry->ListEntry);
                    Log2(("rtDvmFmtMbrReadExtended: %#012RX64::%u: vol%u bType=%#04x fFlags=%#04x offPart=%#012RX64 cbPart=%#012RX64\n",
                          offCurBegin, i, pThis->cPartitions - 1, pEntry->bType, pEntry->fFlags, pEntry->offPart, pEntry->cbPart));
                }
                else
                {
                    pEntry->offPart += offExtBegin;
                    pNext->cExtended++;
                    if (pNext->idxExtended == UINT8_MAX)
                        pNext->idxExtended = (uint8_t)i;
                    else
                    {
                        pEntry->fBad = true;
                        LogRel(("rtDvmFmtMbrReadExtended: Warning! Both #%u and #%u are extended partition table entries! Only following the former\n",
                                i, pNext->idxExtended));
                    }
                    Log2(("rtDvmFmtMbrReadExtended: %#012RX64::%u: ext%u bType=%#04x fFlags=%#04x offPart=%#012RX64 cbPart=%#012RX64\n",
                          offCurBegin, i, pNext->cExtended - 1, pEntry->bType, pEntry->fFlags, pEntry->offPart, pEntry->cbPart));
                }
                pNext->cUsed++;

            }
            /* else: unused */
        }

        /*
         * We're done if we didn't find any extended partition table entry.
         * Otherwise, advance to the next one.
         */
        if (!pNext->cExtended)
            return VINF_SUCCESS;
        pCurEntry   = &pNext->aEntries[pNext->idxExtended];
        offCurBegin = pCurEntry->offPart;
    }
}


static DECLCALLBACK(int) rtDvmFmtMbrOpen(PCRTDVMDISK pDisk, PRTDVMFMT phVolMgrFmt)
{
    int rc;
    size_t const      cbMbr = RT_MAX(512, pDisk->cbSector);
    PRTDVMFMTINTERNAL pThis = (PRTDVMFMTINTERNAL)RTMemAllocZVar(RT_UOFFSETOF_DYN(RTDVMFMTINTERNAL, Primary.abData[cbMbr]));
    if (pThis)
    {
        pThis->pDisk            = pDisk;
        //pThis->cPartitions    = 0;
        RTListInit(&pThis->PartitionHead);
        //pThis->Primary.offOnDisk   = 0;
        //pThis->Primary.pPrevSector = NULL;
        pThis->Primary.fIsPrimary    = true;
        //pThis->Primary.cUsed       = 0;
        //pThis->Primary.cExtended   = 0;
        pThis->Primary.idxExtended   = UINT8_MAX;

        /* We'll use the sector size reported by the disk.

           Though, giiven that the MBR was hardwired to 512 byte sectors, we probably
           should do some probing when the sector size differs from 512, but that can
           wait till there is a real need for it and we've got some semi reliable
           heuristics for doing that. */
        pThis->cbSector = (uint32_t)pDisk->cbSector;
        AssertLogRelMsgStmt(   pThis->cbSector >= 512
                            && pThis->cbSector <= _64K,
                            ("cbSector=%#x\n", pThis->cbSector),
                            pThis->cbSector = 512);

        /*
         * Read the primary MBR.
         */
        uint8_t *pabData = &pThis->Primary.abData[0];
        rc = rtDvmDiskRead(pDisk, 0, pabData, cbMbr);
        if (RT_SUCCESS(rc))
        {
            Assert(pabData[510] == 0x55 && pabData[511] == 0xaa);

            /*
             * Setup basic data for the 4 entries.
             */
            PRTDVMMBRENTRY pEntry     = &pThis->Primary.aEntries[0];
            uint8_t       *pbMbrEntry = &pabData[446];
            uint8_t        idxVisible = 1;
            for (unsigned i = 0; i < 4; i++, pEntry++, pbMbrEntry += 16)
            {
                pEntry->pSector  = &pThis->Primary;
                pEntry->idxTable = (uint8_t)i;
                RTListInit(&pEntry->ListEntry);

                uint8_t const bType  = pbMbrEntry[4];
                if (bType != 0)
                {
                    pEntry->bType    = bType;
                    pEntry->fFlags   = pbMbrEntry[0];
                    pEntry->idxAll   = (uint8_t)(i + 1);

                    rtDvmFmtMbrDecodeChs(&pEntry->FirstChs, &pbMbrEntry[1]);
                    rtDvmFmtMbrDecodeChs(&pEntry->LastChs,  &pbMbrEntry[5]);

                    pEntry->offPart  = RT_MAKE_U32_FROM_U8(pbMbrEntry[0x08 + 0],
                                                           pbMbrEntry[0x08 + 1],
                                                           pbMbrEntry[0x08 + 2],
                                                           pbMbrEntry[0x08 + 3]);
                    pEntry->offPart *= pThis->cbSector;
                    pEntry->cbPart   = RT_MAKE_U32_FROM_U8(pbMbrEntry[0x0c + 0],
                                                           pbMbrEntry[0x0c + 1],
                                                           pbMbrEntry[0x0c + 2],
                                                           pbMbrEntry[0x0c + 3]);
                    pEntry->cbPart  *= pThis->cbSector;
                    if (!RTDVMMBR_IS_EXTENDED(bType))
                    {
                        pEntry->idxVisible = idxVisible++;
                        pEntry->idxLinux   = (uint8_t)(i + 1);
                        pThis->cPartitions++;
                        RTListAppend(&pThis->PartitionHead, &pEntry->ListEntry);
                        Log2(("rtDvmFmtMbrOpen: %u: vol%u bType=%#04x fFlags=%#04x offPart=%#012RX64 cbPart=%#012RX64\n",
                              i, pThis->cPartitions - 1, pEntry->bType, pEntry->fFlags, pEntry->offPart, pEntry->cbPart));
                    }
                    else
                    {
                        pThis->Primary.cExtended++;
                        Log2(("rtDvmFmtMbrOpen: %u: ext%u bType=%#04x fFlags=%#04x offPart=%#012RX64 cbPart=%#012RX64\n",
                              i, pThis->Primary.cExtended - 1, pEntry->bType, pEntry->fFlags, pEntry->offPart, pEntry->cbPart));
                    }
                    pThis->Primary.cUsed++;
                }
                /* else: unused */
            }

            /*
             * Now read any extended partitions.  Since it's no big deal for us, we allow
             * the primary partition table to have more than one extended partition.  However
             * in the extended tables we only allow a single forward link to avoid having to
             * deal with recursion.
             */
            if (pThis->Primary.cExtended > 0)
            {
                uint8_t idxAll   = 5;
                uint8_t idxLinux = 5;
                for (unsigned i = 0; i < 4; i++)
                    if (RTDVMMBR_IS_EXTENDED(pThis->Primary.aEntries[i].bType))
                    {
                        if (pThis->Primary.idxExtended == UINT8_MAX)
                            pThis->Primary.idxExtended = (uint8_t)i;
                        rc = rtDvmFmtMbrReadExtended(pThis, &pThis->Primary.aEntries[i], &idxAll, &idxVisible, &idxLinux);
                        if (RT_FAILURE(rc))
                            break;
                    }
            }
            if (RT_SUCCESS(rc))
            {
                *phVolMgrFmt = pThis;
                return rc;
            }
        }
        rtDvmFmtMbrDestroy(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

static DECLCALLBACK(int) rtDvmFmtMbrInitialize(PCRTDVMDISK pDisk, PRTDVMFMT phVolMgrFmt)
{
    int rc;
    size_t const cbMbr = RT_MAX(512, pDisk->cbSector);
    PRTDVMFMTINTERNAL pThis = (PRTDVMFMTINTERNAL)RTMemAllocZVar(RT_UOFFSETOF_DYN(RTDVMFMTINTERNAL, Primary.abData[cbMbr]));
    if (pThis)
    {
        pThis->pDisk            = pDisk;
        //pThis->cPartitions    = 0;
        RTListInit(&pThis->PartitionHead);
        //pThis->Primary.offOnDisk   = 0
        //pThis->Primary.pPrevSector = NULL;
        pThis->Primary.fIsPrimary    = true;
        //pThis->Primary.cUsed       = 0;
        //pThis->Primary.cExtended   = 0;
        pThis->Primary.idxExtended   = UINT8_MAX;

        /* Setup a new MBR and write it to the disk. */
        uint8_t *pabData = &pThis->Primary.abData[0];
        RT_BZERO(pabData, 512);
        pabData[510] = 0x55;
        pabData[511] = 0xaa;
        rc = rtDvmDiskWrite(pDisk, 0, pabData, cbMbr);
        if (RT_SUCCESS(rc))
        {
            pThis->pDisk = pDisk;
            *phVolMgrFmt = pThis;
        }
        else
            RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

static DECLCALLBACK(void) rtDvmFmtMbrClose(RTDVMFMT hVolMgrFmt)
{
    rtDvmFmtMbrDestroy(hVolMgrFmt);
}

static DECLCALLBACK(int) rtDvmFmtMbrQueryRangeUse(RTDVMFMT hVolMgrFmt, uint64_t off, uint64_t cbRange, bool *pfUsed)
{
    PRTDVMFMTINTERNAL pThis = hVolMgrFmt;

    /*
     * The MBR definitely uses the first 512 bytes, but we consider anything up
     * to 1MB of alignment padding / cylinder gap to be considered in use too.
     *
     * The cylinder gap has been used by several boot managers and boot loaders
     * to store code and data.
     */
    if (off < (uint64_t)_1M)
    {
        *pfUsed = true;
        return VINF_SUCCESS;
    }

    /* Ditto for any extended partition tables. */
    for (uint32_t iPrimary = 0; iPrimary < 4; iPrimary++)
    {
        PRTDVMMBRSECTOR pCur = pThis->Primary.aEntries[iPrimary].pChain;
        while (pCur)
        {
            if (    off           < pCur->offOnDisk + _1M
                &&  off + cbRange > pCur->offOnDisk)
            {
                *pfUsed = true;
                return VINF_SUCCESS;
            }


            if (pCur->idxExtended == UINT8_MAX)
                break;
            pCur = pCur->aEntries[pCur->idxExtended].pChain;
        }

    }

    /* Not in use. */
    *pfUsed = false;
    return VINF_SUCCESS;
}

/** @copydoc RTDVMFMTOPS::pfnQueryDiskUuid */
static DECLCALLBACK(int) rtDvmFmtMbrQueryDiskUuid(RTDVMFMT hVolMgrFmt, PRTUUID pUuid)
{
    PRTDVMFMTINTERNAL pThis = hVolMgrFmt;
    uint32_t idDisk = RT_MAKE_U32_FROM_U8(pThis->Primary.abData[440],
                                          pThis->Primary.abData[441],
                                          pThis->Primary.abData[442],
                                          pThis->Primary.abData[443]);
    if (idDisk != 0)
    {
        RTUuidClear(pUuid);
        pUuid->Gen.u32TimeLow = idDisk;
        return VINF_NOT_SUPPORTED;
    }
    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(uint32_t) rtDvmFmtMbrGetValidVolumes(RTDVMFMT hVolMgrFmt)
{
    PRTDVMFMTINTERNAL pThis = hVolMgrFmt;

    return pThis->cPartitions;
}

static DECLCALLBACK(uint32_t) rtDvmFmtMbrGetMaxVolumes(RTDVMFMT hVolMgrFmt)
{
    NOREF(hVolMgrFmt);
    return 4; /** @todo Add support for EBR? */
}

/**
 * Creates a new volume.
 *
 * @returns IPRT status code.
 * @param   pThis       The MBR volume manager data.
 * @param   pEntry      The MBR entry to create a volume handle for.
 * @param   phVolFmt    Where to store the volume data on success.
 */
static int rtDvmFmtMbrVolumeCreate(PRTDVMFMTINTERNAL pThis, PRTDVMMBRENTRY pEntry, PRTDVMVOLUMEFMT phVolFmt)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = (PRTDVMVOLUMEFMTINTERNAL)RTMemAllocZ(sizeof(RTDVMVOLUMEFMTINTERNAL));
    if (pVol)
    {
        pVol->pVolMgr    = pThis;
        pVol->pEntry     = pEntry;
        *phVolFmt = pVol;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}

static DECLCALLBACK(int) rtDvmFmtMbrQueryFirstVolume(RTDVMFMT hVolMgrFmt, PRTDVMVOLUMEFMT phVolFmt)
{
    PRTDVMFMTINTERNAL pThis = hVolMgrFmt;
    if (pThis->cPartitions != 0)
        return rtDvmFmtMbrVolumeCreate(pThis, RTListGetFirst(&pThis->PartitionHead, RTDVMMBRENTRY, ListEntry), phVolFmt);
    return VERR_DVM_MAP_EMPTY;
}

static DECLCALLBACK(int) rtDvmFmtMbrQueryNextVolume(RTDVMFMT hVolMgrFmt, RTDVMVOLUMEFMT hVolFmt, PRTDVMVOLUMEFMT phVolFmtNext)
{
    PRTDVMFMTINTERNAL       pThis   = hVolMgrFmt;
    PRTDVMVOLUMEFMTINTERNAL pCurVol = hVolFmt;
    if (pCurVol)
    {
        PRTDVMMBRENTRY pNextEntry = RTListGetNext(&pThis->PartitionHead, pCurVol->pEntry, RTDVMMBRENTRY, ListEntry);
        if (pNextEntry)
            return rtDvmFmtMbrVolumeCreate(pThis, pNextEntry, phVolFmtNext);
        return VERR_DVM_MAP_NO_VOLUME;
    }
    if (pThis->cPartitions != 0)
        return rtDvmFmtMbrVolumeCreate(pThis, RTListGetFirst(&pThis->PartitionHead, RTDVMMBRENTRY, ListEntry), phVolFmtNext);
    return VERR_DVM_MAP_EMPTY;
}

/**
 * Helper for rtDvmFmtMbrQueryTableLocations that calculates the padding and/or
 * free space at @a off.
 *
 * Because nothing need to be sorted by start offset, we have to traverse all
 * partition tables to determine this.
 */
static uint64_t rtDvmFmtMbrCalcTablePadding(PRTDVMFMTINTERNAL pThis, uint64_t off)
{
    uint64_t offNext = pThis->pDisk->cbDisk;
    for (unsigned i = 0; i < 4; i++)
    {
        /* Check this primary entry */
        uint64_t offCur = pThis->Primary.aEntries[i].offPart;
        if (offCur >= off && offCur < offNext && pThis->Primary.aEntries[i].bType != 0)
            offNext = offCur;

        /* If it's an extended partition, check the chained ones too. */
        for (PRTDVMMBRSECTOR pCur = pThis->Primary.aEntries[i].pChain;
             pCur != NULL;
             pCur = pCur->idxExtended != UINT8_MAX ? pCur->aEntries[pCur->idxExtended].pChain : NULL)
        {
            for (unsigned j = 0; j < 4; j++)
            {
                offCur = pCur->aEntries[j].offPart;
                if (offCur >= off && offCur < offNext && pCur->aEntries[j].bType != 0)
                    offNext = offCur;
            }
        }
    }
    Assert(offNext >= off);
    return offNext - off;
}

/** @copydoc RTDVMFMTOPS::pfnQueryTableLocations */
static DECLCALLBACK(int) rtDvmFmtMbrQueryTableLocations(RTDVMFMT hVolMgrFmt, uint32_t fFlags, PRTDVMTABLELOCATION paLocations,
                                                        size_t cLocations, size_t *pcActual)
{
    PRTDVMFMTINTERNAL pThis = hVolMgrFmt;
    RT_NOREF(fFlags);

    /*
     * The MBR.
     */
    int     rc = VINF_SUCCESS;
    size_t  iLoc = 0;
    if (cLocations > 0)
    {
        paLocations[iLoc].off       = pThis->Primary.offOnDisk;
        paLocations[iLoc].cb        = pThis->cbSector;
        paLocations[iLoc].cbPadding = rtDvmFmtMbrCalcTablePadding(pThis, 0 + pThis->cbSector);
    }
    else
        rc = VERR_BUFFER_OVERFLOW;
    iLoc++;

    /*
     * Now do the extended partitions.
     *
     * Remember, we only support multiple in the primary MBR, only the first
     * one is honored in the chained ones.
     */
    for (unsigned i = 0; i < 4; i++)
    {
        for (PRTDVMMBRSECTOR pCur = pThis->Primary.aEntries[i].pChain;
             pCur != NULL;
             pCur = pCur->idxExtended != UINT8_MAX ? pCur->aEntries[pCur->idxExtended].pChain : NULL)
        {
            if (cLocations > iLoc)
            {
                paLocations[iLoc].off       = pCur->offOnDisk;
                paLocations[iLoc].cb        = pThis->cbSector;
                paLocations[iLoc].cbPadding = rtDvmFmtMbrCalcTablePadding(pThis, pCur->offOnDisk + pThis->cbSector);
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
            iLoc++;
        }
    }

    /*
     * Return values.
     */
    if (pcActual)
        *pcActual = iLoc;
    else if (cLocations != iLoc && RT_SUCCESS(rc))
    {
        RT_BZERO(&paLocations[iLoc], (cLocations - iLoc) * sizeof(paLocations[0]));
        rc = VERR_BUFFER_UNDERFLOW;
    }
    return rc;
}

static DECLCALLBACK(void) rtDvmFmtMbrVolumeClose(RTDVMVOLUMEFMT hVolFmt)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;

    pVol->pVolMgr    = NULL;
    pVol->pEntry     = NULL;

    RTMemFree(pVol);
}

static DECLCALLBACK(uint64_t) rtDvmFmtMbrVolumeGetSize(RTDVMVOLUMEFMT hVolFmt)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;

    return pVol->pEntry->cbPart;
}

static DECLCALLBACK(int) rtDvmFmtMbrVolumeQueryName(RTDVMVOLUMEFMT hVolFmt, char **ppszVolName)
{
    NOREF(hVolFmt); NOREF(ppszVolName);
    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(RTDVMVOLTYPE) rtDvmFmtMbrVolumeGetType(RTDVMVOLUMEFMT hVolFmt)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;

    uint8_t const bType = pVol->pEntry->bType;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aFs2DvmVolTypes); i++)
        if (g_aFs2DvmVolTypes[i].bFsId == bType)
            return g_aFs2DvmVolTypes[i].enmVolType;

    return RTDVMVOLTYPE_UNKNOWN;
}

static DECLCALLBACK(uint64_t) rtDvmFmtMbrVolumeGetFlags(RTDVMVOLUMEFMT hVolFmt)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;

    uint64_t fFlags = DVMVOLUME_F_CONTIGUOUS;
    if (pVol->pEntry->fFlags & 0x80)
        fFlags |= DVMVOLUME_FLAGS_BOOTABLE | DVMVOLUME_FLAGS_ACTIVE;

    return fFlags;
}

static DECLCALLBACK(int) rtDvmFmtMbrVolumeQueryRange(RTDVMVOLUMEFMT hVolFmt, uint64_t *poffStart, uint64_t *poffLast)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;
    *poffStart = pVol->pEntry->offPart;
    *poffLast  = pVol->pEntry->offPart + pVol->pEntry->cbPart - 1;
    return VINF_SUCCESS;
}

static DECLCALLBACK(bool) rtDvmFmtMbrVolumeIsRangeIntersecting(RTDVMVOLUMEFMT hVolFmt, uint64_t offStart, size_t cbRange,
                                                               uint64_t *poffVol, uint64_t *pcbIntersect)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;

    if (RTDVM_RANGE_IS_INTERSECTING(pVol->pEntry->offPart, pVol->pEntry->cbPart, offStart))
    {
        *poffVol      = offStart - pVol->pEntry->offPart;
        *pcbIntersect = RT_MIN(cbRange, pVol->pEntry->offPart + pVol->pEntry->cbPart - offStart);
        return true;
    }
    return false;
}

/** @copydoc RTDVMFMTOPS::pfnVolumeQueryTableLocation */
static DECLCALLBACK(int) rtDvmFmtMbrVolumeQueryTableLocation(RTDVMVOLUMEFMT hVolFmt, uint64_t *poffTable, uint64_t *pcbTable)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;
    *poffTable = pVol->pEntry->pSector->offOnDisk;
    *pcbTable  = RT_MAX(512, pVol->pVolMgr->pDisk->cbSector);
    return VINF_SUCCESS;
}

/** @copydoc RTDVMFMTOPS::pfnVolumeGetIndex */
static DECLCALLBACK(uint32_t) rtDvmFmtMbrVolumeGetIndex(RTDVMVOLUMEFMT hVolFmt, RTDVMVOLIDX enmIndex)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;
    switch (enmIndex)
    {
        case RTDVMVOLIDX_USER_VISIBLE:
            return pVol->pEntry->idxVisible;
        case RTDVMVOLIDX_ALL:
            return pVol->pEntry->idxAll;
        case RTDVMVOLIDX_IN_TABLE:
            return pVol->pEntry->idxTable;
        case RTDVMVOLIDX_LINUX:
            return pVol->pEntry->idxLinux;

        case RTDVMVOLIDX_INVALID:
        case RTDVMVOLIDX_HOST:
        case RTDVMVOLIDX_END:
        case RTDVMVOLIDX_32BIT_HACK:
            break;
        /* no default! */
    }
    return UINT32_MAX;
}

/** @copydoc RTDVMFMTOPS::pfnVolumeQueryProp */
static DECLCALLBACK(int) rtDvmFmtMbrVolumeQueryProp(RTDVMVOLUMEFMT hVolFmt, RTDVMVOLPROP enmProperty,
                                                    void *pvBuf, size_t cbBuf, size_t *pcbBuf)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;
    switch (enmProperty)
    {
        case RTDVMVOLPROP_MBR_FIRST_CYLINDER:
            *pcbBuf = sizeof(uint16_t);
            Assert(cbBuf >= *pcbBuf);
            *(uint16_t *)pvBuf = pVol->pEntry->FirstChs.uCylinder;
            return VINF_SUCCESS;
        case RTDVMVOLPROP_MBR_LAST_CYLINDER:
            *pcbBuf = sizeof(uint16_t);
            Assert(cbBuf >= *pcbBuf);
            *(uint16_t *)pvBuf = pVol->pEntry->LastChs.uCylinder;
            return VINF_SUCCESS;

        case RTDVMVOLPROP_MBR_FIRST_HEAD:
            *pcbBuf = sizeof(uint8_t);
            Assert(cbBuf >= *pcbBuf);
            *(uint8_t *)pvBuf = pVol->pEntry->FirstChs.uHead;
            return VINF_SUCCESS;
        case RTDVMVOLPROP_MBR_LAST_HEAD:
            *pcbBuf = sizeof(uint8_t);
            Assert(cbBuf >= *pcbBuf);
            *(uint8_t *)pvBuf = pVol->pEntry->LastChs.uHead;
            return VINF_SUCCESS;

        case RTDVMVOLPROP_MBR_FIRST_SECTOR:
            *pcbBuf = sizeof(uint8_t);
            Assert(cbBuf >= *pcbBuf);
            *(uint8_t *)pvBuf = pVol->pEntry->FirstChs.uSector;
            return VINF_SUCCESS;
        case RTDVMVOLPROP_MBR_LAST_SECTOR:
            *pcbBuf = sizeof(uint8_t);
            Assert(cbBuf >= *pcbBuf);
            *(uint8_t *)pvBuf = pVol->pEntry->LastChs.uSector;
            return VINF_SUCCESS;

        case RTDVMVOLPROP_MBR_TYPE:
            *pcbBuf = sizeof(uint8_t);
            Assert(cbBuf >= *pcbBuf);
            *(uint8_t *)pvBuf = pVol->pEntry->bType;
            return VINF_SUCCESS;

        case RTDVMVOLPROP_GPT_TYPE:
        case RTDVMVOLPROP_GPT_UUID:
            return VERR_NOT_SUPPORTED;

        case RTDVMVOLPROP_INVALID:
        case RTDVMVOLPROP_END:
        case RTDVMVOLPROP_32BIT_HACK:
            break;
        /* not default! */
    }
    RT_NOREF(cbBuf);
    AssertFailed();
    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(int) rtDvmFmtMbrVolumeRead(RTDVMVOLUMEFMT hVolFmt, uint64_t off, void *pvBuf, size_t cbRead)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;
    AssertReturn(off + cbRead <= pVol->pEntry->cbPart, VERR_INVALID_PARAMETER);

    return rtDvmDiskReadUnaligned(pVol->pVolMgr->pDisk, pVol->pEntry->offPart + off, pvBuf, cbRead);
}

static DECLCALLBACK(int) rtDvmFmtMbrVolumeWrite(RTDVMVOLUMEFMT hVolFmt, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    PRTDVMVOLUMEFMTINTERNAL pVol = hVolFmt;
    AssertReturn(off + cbWrite <= pVol->pEntry->cbPart, VERR_INVALID_PARAMETER);

    return rtDvmDiskWrite(pVol->pVolMgr->pDisk, pVol->pEntry->offPart + off, pvBuf, cbWrite);
}

DECL_HIDDEN_CONST(const RTDVMFMTOPS) g_rtDvmFmtMbr =
{
    /* pszFmt */
    "MBR",
    /* enmFormat */
    RTDVMFORMATTYPE_MBR,
    /* pfnProbe */
    rtDvmFmtMbrProbe,
    /* pfnOpen */
    rtDvmFmtMbrOpen,
    /* pfnInitialize */
    rtDvmFmtMbrInitialize,
    /* pfnClose */
    rtDvmFmtMbrClose,
    /* pfnQueryRangeUse */
    rtDvmFmtMbrQueryRangeUse,
    /* pfnQueryDiskUuid */
    rtDvmFmtMbrQueryDiskUuid,
    /* pfnGetValidVolumes */
    rtDvmFmtMbrGetValidVolumes,
    /* pfnGetMaxVolumes */
    rtDvmFmtMbrGetMaxVolumes,
    /* pfnQueryFirstVolume */
    rtDvmFmtMbrQueryFirstVolume,
    /* pfnQueryNextVolume */
    rtDvmFmtMbrQueryNextVolume,
    /* pfnQueryTableLocations */
    rtDvmFmtMbrQueryTableLocations,
    /* pfnVolumeClose */
    rtDvmFmtMbrVolumeClose,
    /* pfnVolumeGetSize */
    rtDvmFmtMbrVolumeGetSize,
    /* pfnVolumeQueryName */
    rtDvmFmtMbrVolumeQueryName,
    /* pfnVolumeGetType */
    rtDvmFmtMbrVolumeGetType,
    /* pfnVolumeGetFlags */
    rtDvmFmtMbrVolumeGetFlags,
    /* pfnVolumeQueryRange */
    rtDvmFmtMbrVolumeQueryRange,
    /* pfnVOlumeIsRangeIntersecting */
    rtDvmFmtMbrVolumeIsRangeIntersecting,
    /* pfnVolumeQueryTableLocation */
    rtDvmFmtMbrVolumeQueryTableLocation,
    /* pfnVolumeGetIndex */
    rtDvmFmtMbrVolumeGetIndex,
    /* pfnVolumeQueryProp */
    rtDvmFmtMbrVolumeQueryProp,
    /* pfnVolumeRead */
    rtDvmFmtMbrVolumeRead,
    /* pfnVolumeWrite */
    rtDvmFmtMbrVolumeWrite
};

