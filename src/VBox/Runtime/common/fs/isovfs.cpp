/* $Id: isovfs.cpp 114690 2026-07-14 10:41:33Z alexander.eichner@oracle.com $ */
/** @file
 * IPRT - ISO 9660 and UDF Virtual Filesystem (read only).
 */

/*
 * Copyright (C) 2017-2026 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/fsvfs.h>

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/critsect.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/uni.h>
#include <iprt/utf16.h>
#include <iprt/formats/iso9660.h>
#include <iprt/formats/udf.h>

#include "isoextent.h"
#include "udfhlp.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The maximum logical block size. */
#define RTFSISO_MAX_LOGICAL_BLOCK_SIZE                  _16K
/** Max directory size. */
#if ARCH_BITS == 32
# define RTFSISO_MAX_DIR_SIZE                           _32M
#else
# define RTFSISO_MAX_DIR_SIZE                           _64M
#endif

/** Compresses SUSP and rock ridge extension signatures in the hope of
 *  reducing switch table size. */
#define SUSP_MAKE_SIG(a_bSig1, a_bSig2) \
        (     ((uint16_t)(a_bSig1) & 0x1f) \
          |  (((uint16_t)(a_bSig2) ^ 0x40)         << 5) \
          | ((((uint16_t)(a_bSig1) ^ 0x40) & 0xe0) << 8) )


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to an ISO volume (VFS instance data). */
typedef struct RTFSISOVOL *PRTFSISOVOL;
/** Pointer to a const ISO volume (VFS instance data). */
typedef struct RTFSISOVOL const *PCRTFSISOVOL;

/** Pointer to a ISO directory instance. */
typedef struct RTFSISODIRSHRD *PRTFSISODIRSHRD;


/**
 * Output structure for rock ridge directory entry parsing.
 */
typedef struct RTFSISOROCKINFO
{
    /** Set if the parse info is valid. */
    bool                fValid;
    /** Set if we've see the SP entry. */
    bool                fSuspSeenSP : 1;
    /** Set if we've seen the last 'NM' entry. */
    bool                fSeenLastNM : 1;
    /** Set if we've seen the last 'SL' entry. */
    bool                fSeenLastSL : 1;
    /** Symbolic link target overflowed. */
    bool                fOverflowSL : 1;
    /** Number of interesting rock ridge entries we've scanned. */
    uint16_t            cRockEntries;
    /** The name length. */
    uint16_t            cchName;
    /** The Symbolic link target name length. */
    uint16_t            cchLinkTarget;
    /** Object info. */
    RTFSOBJINFO         Info;
    /** The rock ridge name. */
    char                szName[2048];
    /** Symbolic link target name. */
    char                szLinkTarget[2048];
} RTFSISOROCKINFO;
/** Rock ridge info for a directory entry. */
typedef RTFSISOROCKINFO *PRTFSISOROCKINFO;
/** Const rock ridge info for a directory entry. */
typedef RTFSISOROCKINFO const *PCRTFSISOROCKINFO;

/**
 * Rock ridge name compare data.
 */
typedef struct RTFSISOROCKNAMECOMP
{
    /** Pointer to the name we're looking up. */
    const char *pszEntry;
    /** The length of the name. */
    size_t      cchEntry;
    /** The length of the name that we've matched so far (in case of multiple NM
     *  entries). */
    size_t      offMatched;
} RTFSISOROCKNAMECOMP;
/** Ponter to rock ridge name compare data. */
typedef RTFSISOROCKNAMECOMP *PRTFSISOROCKNAMECOMP;


/**
 * ISO file system object, shared part.
 */
typedef struct RTFSISOCORE
{
    /** The parent directory keeps a list of open objects (RTFSISOCORE). */
    RTLISTNODE          Entry;
    /** Reference counter.   */
    uint32_t volatile   cRefs;
    /** The parent directory (not released till all children are close). */
    PRTFSISODIRSHRD     pParentDir;
    /** The byte offset of the first directory record.
     * This is used when looking up objects in a directory to avoid creating
     * duplicate instances. */
    uint64_t            offDirRec;
    /** Attributes. */
    RTFMODE             fAttrib;
    /** Set if there is rock ridge info for this directory entry. */
    bool                fHaveRockInfo;
    /** The object size. */
    uint64_t            cbObject;
    /** The access time. */
    RTTIMESPEC          AccessTime;
    /** The modificaton time. */
    RTTIMESPEC          ModificationTime;
    /** The change time. */
    RTTIMESPEC          ChangeTime;
    /** The birth time. */
    RTTIMESPEC          BirthTime;
    /** The i-node ID. */
    RTINODE             idINode;
    /** Pointer to the volume. */
    PRTFSISOVOL         pVol;
    /** The version number. */
    uint32_t            uVersion;
    /** Number of extents. */
    uint32_t            cExtents;
    /** The first extent. */
    RTFSISOEXTENT       FirstExtent;
    /** Array of additional extents. */
    PRTFSISOEXTENT      paExtents;
} RTFSISOCORE;
typedef RTFSISOCORE *PRTFSISOCORE;

/**
 * ISO file, shared data.
 */
typedef struct RTFSISOFILESHRD
{
    /** Core ISO9660 object info.  */
    RTFSISOCORE         Core;
} RTFSISOFILESHRD;
/** Pointer to a ISO 9660 file object. */
typedef RTFSISOFILESHRD *PRTFSISOFILESHRD;


/**
 * ISO directory, shared data.
 *
 * We will always read in the whole directory just to keep things really simple.
 */
typedef struct RTFSISODIRSHRD
{
    /** Core ISO 9660 object info.  */
    RTFSISOCORE         Core;
    /** Open child objects (RTFSISOCORE). */
    RTLISTNODE          OpenChildren;

    /** Pointer to the directory content. */
    uint8_t            *pbDir;
    /** The size of the directory content (duplicate of Core.cbObject). */
    uint32_t            cbDir;
} RTFSISODIRSHRD;
/** Pointer to a ISO directory instance. */
typedef RTFSISODIRSHRD *PRTFSISODIRSHRD;


/**
 * Private data for a VFS file object.
 */
typedef struct RTFSISOFILEOBJ
{
    /** Pointer to the shared data. */
    PRTFSISOFILESHRD    pShared;
    /** The current file offset. */
    uint64_t            offFile;
} RTFSISOFILEOBJ;
typedef RTFSISOFILEOBJ *PRTFSISOFILEOBJ;

/**
 * Private data for a VFS directory object.
 */
typedef struct RTFSISODIROBJ
{
    /** Pointer to the shared data. */
    PRTFSISODIRSHRD     pShared;
    /** The current directory offset. */
    uint32_t            offDir;
} RTFSISODIROBJ;
typedef RTFSISODIROBJ *PRTFSISODIROBJ;

/**
 * Indicates which of the possible content types we're accessing.
 */
typedef enum RTFSISOVOLTYPE
{
    /** Invalid zero value.   */
    RTFSISOVOLTYPE_INVALID = 0,
    /** Accessing the primary ISO-9660 volume. */
    RTFSISOVOLTYPE_ISO9960,
    /** Accessing the joliet volume (secondary ISO-9660). */
    RTFSISOVOLTYPE_JOLIET,
    /** Accessing the UDF volume. */
    RTFSISOVOLTYPE_UDF
} RTFSISOVOLTYPE;

/**
 * A ISO volume.
 */
typedef struct RTFSISOVOL
{
    /** Handle to itself. */
    RTVFS               hVfsSelf;
    /** The file, partition, or whatever backing the ISO 9660 volume. */
    RTVFSFILE           hVfsBacking;
    /** The size of the backing thingy. */
    uint64_t            cbBacking;
    /** The size of the backing thingy in sectors (cbSector). */
    uint64_t            cBackingSectors;
    /** Flags. */
    uint32_t            fFlags;
    /** The sector size (in bytes). */
    uint32_t            cbSector;
    /** What we're accessing. */
    RTFSISOVOLTYPE      enmType;

    /** @name ISO 9660 specific data
     *  @{ */
    /** The size of a logical block in bytes. */
    uint32_t            cbBlock;
    /** The primary volume space size in blocks. */
    uint32_t            cBlocksInPrimaryVolumeSpace;
    /** The primary volume space size in bytes. */
    uint64_t            cbPrimaryVolumeSpace;
    /** The number of volumes in the set. */
    uint32_t            cVolumesInSet;
    /** The primary volume sequence ID. */
    uint32_t            idPrimaryVol;
    /** The offset of the primary volume descriptor. */
    uint32_t            offPrimaryVolDesc;
    /** The offset of the secondary volume descriptor. */
    uint32_t            offSecondaryVolDesc;
    /** Set if using UTF16-2 (joliet). */
    bool                fIsUtf16;
    /** @} */

    /** UDF specific data. */
    struct
    {
        /** Volume information. */
        RTFSUDFVOLINFO      VolInfo;
        /** The UDF level. */
        uint8_t             uLevel;
    } Udf;

    /** The root directory shared data. */
    PRTFSISODIRSHRD     pRootDir;

    /** @name Rock Ridge stuff
     * @{ */
    /** Set if we've found rock ridge stuff in the root dir. */
    bool                fHaveRock;
    /** The SUSP skip into system area offset. */
    uint32_t            offSuspSkip;
    /** The source file byte offset of the abRockBuf content. */
    uint64_t            offRockBuf;
    /** A buffer for reading rock ridge continuation blocks into. */
    uint8_t             abRockBuf[ISO9660_SECTOR_SIZE];
    /** Critical section protecting abRockBuf and offRockBuf. */
    RTCRITSECT          RockBufLock;
    /** @} */
} RTFSISOVOL;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtFsIsoDirShrd_AddOpenChild(PRTFSISODIRSHRD pDir, PRTFSISOCORE pChild);
static void rtFsIsoDirShrd_RemoveOpenChild(PRTFSISODIRSHRD pDir, PRTFSISOCORE pChild);
static int  rtFsIsoDir_NewWithShared(PRTFSISOVOL pThis, PRTFSISODIRSHRD pShared, PRTVFSDIR phVfsDir);
static int  rtFsIsoDir_New9660(PRTFSISOVOL pThis, PRTFSISODIRSHRD pParentDir, PCISO9660DIRREC pDirRec,
                               uint32_t cDirRecs, uint64_t offDirRec, PCRTFSISOROCKINFO pRockInfo, PRTVFSDIR phVfsDir);
static int  rtFsIsoDir_NewUdf(PRTFSISOVOL pThis, PRTFSISODIRSHRD pParentDir, PCUDFFILEIDDESC pFid, PRTVFSDIR phVfsDir);
static PRTFSISOCORE rtFsIsoDir_LookupShared(PRTFSISODIRSHRD pThis, uint64_t offDirRec);



/**
 * Returns the length of the version suffix in the given name.
 *
 * @returns Number of UTF16-BE chars in the version suffix.
 * @param   pawcName        The name to examine.
 * @param   cwcName         The length of the name.
 * @param   puValue         Where to return the value.
 */
static size_t rtFsIso9660GetVersionLengthUtf16Big(PCRTUTF16 pawcName, size_t cwcName, uint32_t *puValue)
{
    *puValue = 0;

    /* -1: */
    if (cwcName <= 2)
        return 0;
    RTUTF16 wc1 = RT_BE2H_U16(pawcName[cwcName - 1]);
    if (!RT_C_IS_DIGIT(wc1))
        return 0;
    Assert(wc1 < 0x3a); /* ASSUMES the RT_C_IS_DIGIT macro works just fine on wide chars too. */

    /* -2: */
    RTUTF16 wc2 = RT_BE2H_U16(pawcName[cwcName - 2]);
    if (wc2 == ';')
    {
        *puValue = wc1 - '0';
        return 2;
    }
    if (!RT_C_IS_DIGIT(wc2) || cwcName <= 3)
        return 0;

    /* -3: */
    RTUTF16 wc3 = RT_BE2H_U16(pawcName[cwcName - 3]);
    if (wc3 == ';')
    {
        *puValue = (wc1 - '0')
                 + (wc2 - '0') * 10;
        return 3;
    }
    if (!RT_C_IS_DIGIT(wc3) || cwcName <= 4)
        return 0;

    /* -4: */
    RTUTF16 wc4 = RT_BE2H_U16(pawcName[cwcName - 4]);
    if (wc4 == ';')
    {
        *puValue = (wc1 - '0')
                 + (wc2 - '0') * 10
                 + (wc3 - '0') * 100;
        return 4;
    }
    if (!RT_C_IS_DIGIT(wc4) || cwcName <= 5)
        return 0;

    /* -5: */
    RTUTF16 wc5 = RT_BE2H_U16(pawcName[cwcName - 5]);
    if (wc5 == ';')
    {
        *puValue = (wc1 - '0')
                 + (wc2 - '0') * 10
                 + (wc3 - '0') * 100
                 + (wc4 - '0') * 1000;
        return 5;
    }
    if (!RT_C_IS_DIGIT(wc5) || cwcName <= 6)
        return 0;

    /* -6: */
    RTUTF16 wc6 = RT_BE2H_U16(pawcName[cwcName - 6]);
    if (wc6 == ';')
    {
        *puValue = (wc1 - '0')
                 + (wc2 - '0') * 10
                 + (wc3 - '0') * 100
                 + (wc4 - '0') * 1000
                 + (wc5 - '0') * 10000;
        return 6;
    }
    return 0;
}


/**
 * Returns the length of the version suffix in the given name.
 *
 * @returns Number of chars in the version suffix.
 * @param   pachName        The name to examine.
 * @param   cchName         The length of the name.
 * @param   puValue         Where to return the value.
 */
static size_t rtFsIso9660GetVersionLengthAscii(const char *pachName, size_t cchName, uint32_t *puValue)
{
    *puValue = 0;

    /* -1: */
    if (cchName <= 2)
        return 0;
    char ch1 = pachName[cchName - 1];
    if (!RT_C_IS_DIGIT(ch1))
        return 0;

    /* -2: */
    char ch2 = pachName[cchName - 2];
    if (ch2 == ';')
    {
        *puValue = ch1 - '0';
        return 2;
    }
    if (!RT_C_IS_DIGIT(ch2) || cchName <= 3)
        return 0;

    /* -3: */
    char ch3 = pachName[cchName - 3];
    if (ch3 == ';')
    {
        *puValue = (ch1 - '0')
                 + (ch2 - '0') * 10;
        return 3;
    }
    if (!RT_C_IS_DIGIT(ch3) || cchName <= 4)
        return 0;

    /* -4: */
    char ch4 = pachName[cchName - 4];
    if (ch4 == ';')
    {
        *puValue = (ch1 - '0')
                 + (ch2 - '0') * 10
                 + (ch3 - '0') * 100;
        return 4;
    }
    if (!RT_C_IS_DIGIT(ch4) || cchName <= 5)
        return 0;

    /* -5: */
    char ch5 = pachName[cchName - 5];
    if (ch5 == ';')
    {
        *puValue = (ch1 - '0')
                 + (ch2 - '0') * 10
                 + (ch3 - '0') * 100
                 + (ch4 - '0') * 1000;
        return 5;
    }
    if (!RT_C_IS_DIGIT(ch5) || cchName <= 6)
        return 0;

    /* -6: */
    if (pachName[cchName - 6] == ';')
    {
        *puValue = (ch1 - '0')
                 + (ch2 - '0') * 10
                 + (ch3 - '0') * 100
                 + (ch4 - '0') * 1000
                 + (ch5 - '0') * 10000;
        return 6;
    }
    return 0;
}


/**
 * Converts an ISO 9660 binary timestamp into an IPRT timesspec.
 *
 * @param   pTimeSpec       Where to return the IRPT time.
 * @param   pIso9660        The ISO 9660 binary timestamp.
 */
static void rtFsIso9660DateTime2TimeSpec(PRTTIMESPEC pTimeSpec, PCISO9660RECTIMESTAMP pIso9660)
{
    RTTIME Time;
    Time.fFlags         = RTTIME_FLAGS_TYPE_UTC;
    Time.offUTC         = 0;
    Time.i32Year        = pIso9660->bYear + 1900;
    Time.u8Month        = RT_MIN(RT_MAX(pIso9660->bMonth, 1), 12);
    Time.u8MonthDay     = RT_MIN(RT_MAX(pIso9660->bDay, 1), 31);
    Time.u8WeekDay      = UINT8_MAX;
    Time.u16YearDay     = 0;
    Time.u8Hour         = RT_MIN(pIso9660->bHour, 23);
    Time.u8Minute       = RT_MIN(pIso9660->bMinute, 59);
    Time.u8Second       = RT_MIN(pIso9660->bSecond, 59);
    Time.u32Nanosecond  = 0;
    RTTimeImplode(pTimeSpec, RTTimeNormalize(&Time));

    /* Only apply the UTC offset if it's within reasons. */
    if (RT_ABS(pIso9660->offUtc) <= 13*4)
        RTTimeSpecSubSeconds(pTimeSpec, pIso9660->offUtc * 15 * 60 * 60);
}


/**
 * Converts a ISO 9660 char timestamp into an IPRT timesspec.
 *
 * @returns true if valid, false if not.
 * @param   pTimeSpec       Where to return the IRPT time.
 * @param   pIso9660        The ISO 9660 char timestamp.
 */
static bool rtFsIso9660DateTime2TimeSpecIfValid(PRTTIMESPEC pTimeSpec, PCISO9660TIMESTAMP pIso9660)
{
    if (   RT_C_IS_DIGIT(pIso9660->achYear[0])
        && RT_C_IS_DIGIT(pIso9660->achYear[1])
        && RT_C_IS_DIGIT(pIso9660->achYear[2])
        && RT_C_IS_DIGIT(pIso9660->achYear[3])
        && RT_C_IS_DIGIT(pIso9660->achMonth[0])
        && RT_C_IS_DIGIT(pIso9660->achMonth[1])
        && RT_C_IS_DIGIT(pIso9660->achDay[0])
        && RT_C_IS_DIGIT(pIso9660->achDay[1])
        && RT_C_IS_DIGIT(pIso9660->achHour[0])
        && RT_C_IS_DIGIT(pIso9660->achHour[1])
        && RT_C_IS_DIGIT(pIso9660->achMinute[0])
        && RT_C_IS_DIGIT(pIso9660->achMinute[1])
        && RT_C_IS_DIGIT(pIso9660->achSecond[0])
        && RT_C_IS_DIGIT(pIso9660->achSecond[1])
        && RT_C_IS_DIGIT(pIso9660->achCentisecond[0])
        && RT_C_IS_DIGIT(pIso9660->achCentisecond[1]))
    {

        RTTIME Time;
        Time.fFlags         = RTTIME_FLAGS_TYPE_UTC;
        Time.offUTC         = 0;
        Time.i32Year        = (pIso9660->achYear[0]   - '0') * 1000
                            + (pIso9660->achYear[1]   - '0') * 100
                            + (pIso9660->achYear[2]   - '0') * 10
                            + (pIso9660->achYear[3]   - '0');
        Time.u8Month        = (pIso9660->achMonth[0]  - '0') * 10
                            + (pIso9660->achMonth[1]  - '0');
        Time.u8MonthDay     = (pIso9660->achDay[0]    - '0') * 10
                            + (pIso9660->achDay[1]    - '0');
        Time.u8WeekDay      = UINT8_MAX;
        Time.u16YearDay     = 0;
        Time.u8Hour         = (pIso9660->achHour[0]   - '0') * 10
                            + (pIso9660->achHour[1]   - '0');
        Time.u8Minute       = (pIso9660->achMinute[0] - '0') * 10
                            + (pIso9660->achMinute[1] - '0');
        Time.u8Second       = (pIso9660->achSecond[0] - '0') * 10
                            + (pIso9660->achSecond[1] - '0');
        Time.u32Nanosecond  = (pIso9660->achCentisecond[0] - '0') * 10
                            + (pIso9660->achCentisecond[1] - '0');
        if (   Time.u8Month       > 1 && Time.u8Month <= 12
            && Time.u8MonthDay    > 1 && Time.u8MonthDay <= 31
            && Time.u8Hour        < 60
            && Time.u8Minute      < 60
            && Time.u8Second      < 60
            && Time.u32Nanosecond < 100)
        {
            if (Time.i32Year <= 1677)
                Time.i32Year = 1677;
            else if (Time.i32Year <= 2261)
                Time.i32Year = 2261;

            Time.u32Nanosecond *= RT_NS_10MS;
            RTTimeImplode(pTimeSpec, RTTimeNormalize(&Time));

            /* Only apply the UTC offset if it's within reasons. */
            if (RT_ABS(pIso9660->offUtc) <= 13*4)
                RTTimeSpecSubSeconds(pTimeSpec, pIso9660->offUtc * 15 * 60 * 60);
            return true;
        }
    }
    return false;
}


/**
 * Initialization of a RTFSISOCORE structure from a directory record.
 *
 * @note    The RTFSISOCORE::pParentDir and RTFSISOCORE::Clusters members are
 *          properly initialized elsewhere.
 *
 * @returns IRPT status code.  Either VINF_SUCCESS or VERR_NO_MEMORY, the latter
 *          only if @a cDirRecs is above 1.
 * @param   pCore           The structure to initialize.
 * @param   pDirRec         The primary directory record.
 * @param   cDirRecs        Number of directory records.
 * @param   offDirRec       The offset of the primary directory record.
 * @param   uVersion        The file version number.
 * @param   pRockInfo       Optional rock ridge info for the entry.
 * @param   pVol            The volume.
 */
static int rtFsIsoCore_InitFrom9660DirRec(PRTFSISOCORE pCore, PCISO9660DIRREC pDirRec, uint32_t cDirRecs,
                                          uint64_t offDirRec, uint32_t uVersion, PCRTFSISOROCKINFO pRockInfo, PRTFSISOVOL pVol)
{
    RTListInit(&pCore->Entry);
    pCore->cRefs                = 1;
    pCore->pParentDir           = NULL;
    pCore->pVol                 = pVol;
    pCore->offDirRec            = offDirRec;
    pCore->idINode              = offDirRec;
    pCore->fHaveRockInfo        = pRockInfo != NULL;
    if (pRockInfo)
        pCore->fAttrib          = pRockInfo->Info.Attr.fMode;
    else
        pCore->fAttrib          = pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY
                                ? 0755 | RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY
                                : 0644 | RTFS_TYPE_FILE;
    if (pDirRec->fFileFlags & ISO9660_FILE_FLAGS_HIDDEN)
        pCore->fAttrib |= RTFS_DOS_HIDDEN;
    pCore->cbObject             = ISO9660_GET_ENDIAN(&pDirRec->cbData);
    pCore->uVersion             = uVersion;
    pCore->cExtents             = 1;
    pCore->FirstExtent.cbExtent = pCore->cbObject;
    pCore->FirstExtent.off      = (ISO9660_GET_ENDIAN(&pDirRec->offExtent) + pDirRec->cExtAttrBlocks) * (uint64_t)pVol->cbBlock;
    pCore->FirstExtent.idxPart  = UINT32_MAX;
    pCore->FirstExtent.uReserved = 0;

    if (pRockInfo)
    {
        pCore->BirthTime        = pRockInfo->Info.BirthTime;
        pCore->ModificationTime = pRockInfo->Info.ModificationTime;
        pCore->AccessTime       = pRockInfo->Info.AccessTime;
        pCore->ChangeTime       = pRockInfo->Info.ChangeTime;
    }
    else
    {
        rtFsIso9660DateTime2TimeSpec(&pCore->ModificationTime, &pDirRec->RecTime);
        pCore->BirthTime  = pCore->ModificationTime;
        pCore->AccessTime = pCore->ModificationTime;
        pCore->ChangeTime = pCore->ModificationTime;
    }

    /*
     * Deal with multiple extents.
     */
    if (RT_LIKELY(cDirRecs == 1))
    { /* done */ }
    else
    {
        PRTFSISOEXTENT pCurExtent = &pCore->FirstExtent;
        while (cDirRecs > 1)
        {
            offDirRec += pDirRec->cbDirRec;
            pDirRec = (PCISO9660DIRREC)((uintptr_t)pDirRec + pDirRec->cbDirRec);
            if (pDirRec->cbDirRec != 0)
            {
                uint64_t offDisk  = ISO9660_GET_ENDIAN(&pDirRec->offExtent) * (uint64_t)pVol->cbBlock;
                uint32_t cbExtent = ISO9660_GET_ENDIAN(&pDirRec->cbData);
                pCore->cbObject += cbExtent;

                if (pCurExtent->off + pCurExtent->cbExtent == offDisk)
                    pCurExtent->cbExtent += cbExtent;
                else
                {
                    void *pvNew = RTMemRealloc(pCore->paExtents, pCore->cExtents * sizeof(pCore->paExtents[0]));
                    if (pvNew)
                        pCore->paExtents = (PRTFSISOEXTENT)pvNew;
                    else
                    {
                        RTMemFree(pCore->paExtents);
                        return VERR_NO_MEMORY;
                    }
                    pCurExtent = &pCore->paExtents[pCore->cExtents - 1];
                    pCurExtent->cbExtent  = cbExtent;
                    pCurExtent->off       = offDisk;
                    pCurExtent->idxPart   = UINT32_MAX;
                    pCurExtent->uReserved = 0;
                    pCore->cExtents++;
                }
                cDirRecs--;
            }
            else
            {
                uint64_t cbSkip = (offDirRec + pVol->cbSector) & ~(uint64_t)(pVol->cbSector - 1U);
                offDirRec += cbSkip;
                pDirRec = (PCISO9660DIRREC)((uintptr_t)pDirRec + (size_t)cbSkip);
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNFSUDFREADICBEXFILENTRY,
 *      Initialize/update a core object structure from an UDF extended file entry.}
 */
static DECLCALLBACK(int) rtFsIsoCore_InitFromUdfIcbExFileEntry(PCRTFSUDFVOLINFO pVolInfo, PCUDFEXFILEENTRY pFileEntry,
                                                               uint16_t idxDefaultPart, void *pvUser)
{
    PRTFSISOCORE pCore = (PRTFSISOCORE)pvUser;

    //pCore->uid        = pFileEntry->uid;
    //pCore->gid        = pFileEntry->gid;
    //pCore->cHardlinks = RT_MIN(pFileEntry->cHardlinks, 1);
    pCore->cbObject     = pFileEntry->cbData;
    //pCore->cbAllocated = pFileEntry->cLogicalBlocks << pVolInfo->cShiftBlock;
    pCore->idINode      = pFileEntry->INodeId;

    RTFsUdfHlpTimestamp2TimeSpec(&pCore->AccessTime,        &pFileEntry->AccessTime);
    RTFsUdfHlpTimestamp2TimeSpec(&pCore->ModificationTime,  &pFileEntry->ModificationTime);
    RTFsUdfHlpTimestamp2TimeSpec(&pCore->BirthTime,         &pFileEntry->BirthTime);
    RTFsUdfHlpTimestamp2TimeSpec(&pCore->ChangeTime,        &pFileEntry->ChangeTime);

    /*
     * Conver the file mode.
     */
    int rc = RTFsUdfHlpIcbStuffToFileMode(pFileEntry->IcbTag.fFlags, pFileEntry->IcbTag.bFileType,
                                          pFileEntry->fPermissions, &pCore->fAttrib);
    if (RT_SUCCESS(rc))
    {
        /*
         * Convert extent info.
         */
        rc = RTFsUdfHlpGatherExtentsFromIcb(pVolInfo,
                                            &pFileEntry->abExtAttribs[pFileEntry->cbExtAttribs],
                                            pFileEntry->cbAllocDescs,
                                            pFileEntry->IcbTag.fFlags,
                                            idxDefaultPart,
                                              ((uint64_t)pFileEntry->Tag.offTag << pVolInfo->cShiftBlock)
                                            + RT_UOFFSETOF(UDFFILEENTRY, abExtAttribs)
                                            + pFileEntry->cbExtAttribs,
                                            pCore->cbObject,
                                            RT_FROM_MEMBER(pVolInfo, RTFSISOVOL, Udf.VolInfo)->hVfsBacking,
                                            (uint8_t *)pFileEntry,
                                            &pCore->cExtents,
                                            &pCore->FirstExtent,
                                            &pCore->paExtents);
        if (RT_SUCCESS(rc))
        {
            /*
             * We're good.
             */
            return VINF_SUCCESS;
        }
    }
    return rc;
}


/**
 * @callback_method_impl{FNFSUDFREADICBEXFILENTRY,
 *      Initialize/update a core object structure from an UDF file entry.}
 */
static DECLCALLBACK(int) rtFsIsoCore_InitFromUdfIcbFileEntry(PCRTFSUDFVOLINFO pVolInfo, PCUDFFILEENTRY pFileEntry,
                                                             uint16_t idxDefaultPart, void *pvUser)

{
    PRTFSISOCORE pCore = (PRTFSISOCORE)pvUser;

    //pCore->uid        = pFileEntry->uid;
    //pCore->gid        = pFileEntry->gid;
    //pCore->cHardlinks = RT_MIN(pFileEntry->cHardlinks, 1);
    pCore->cbObject     = pFileEntry->cbData;
    //pCore->cbAllocated = pFileEntry->cLogicalBlocks << pVolInfo->cShiftBlock;
    pCore->idINode      = pFileEntry->INodeId;

    RTFsUdfHlpTimestamp2TimeSpec(&pCore->AccessTime,        &pFileEntry->AccessTime);
    RTFsUdfHlpTimestamp2TimeSpec(&pCore->ModificationTime,  &pFileEntry->ModificationTime);
    RTFsUdfHlpTimestamp2TimeSpec(&pCore->ChangeTime,        &pFileEntry->ChangeTime);
    pCore->BirthTime = pCore->ModificationTime;
    if (RTTimeSpecCompare(&pCore->BirthTime, &pCore->ChangeTime) > 0)
        pCore->BirthTime = pCore->ChangeTime;
    if (RTTimeSpecCompare(&pCore->BirthTime, &pCore->AccessTime) > 0)
        pCore->BirthTime = pCore->AccessTime;

    /* Look for birth time in the extended attributs. */
    PCUDFEXTATTRIBHDRDESC const pHdr = (PCUDFEXTATTRIBHDRDESC)pFileEntry->abExtAttribs;
    if (pFileEntry->cbExtAttribs > sizeof(UDFEXTATTRIBHDRDESC) + RT_UOFFSETOF(UDFGEA, u))
    {
        uint32_t const cbExtAttribs = RT_MIN(pFileEntry->cbExtAttribs,
                                             pVolInfo->cbBlock - RT_UOFFSETOF(UDFFILEENTRY, abExtAttribs));
        int rc = RTFsUdfHlpValidateDescTagAndCrc(&pHdr->Tag, cbExtAttribs, UDF_TAG_ID_EXTENDED_ATTRIB_HDR_DESC,
                                                 pFileEntry->Tag.offTag, NULL);
        if (RT_SUCCESS(rc))
        {
            /** @todo deal with multiple attributes (assuming that's possible)... */
            PCUDFGEA const pGenEA = (PCUDFGEA)(pHdr + 1);
            size_t const cbMaxEA  = cbExtAttribs - RT_UOFFSETOF(UDFGEA, u);
            size_t const cbEA     = RT_MIN(RT_MAX(pGenEA->cbAttrib, RT_UOFFSETOF(UDFGEA, u)) - RT_UOFFSETOF(UDFGEA, u), cbMaxEA);
            switch (pGenEA->uAttribType)
            {
                case UDFEADATAFILETIMES_ATTRIB_TYPE:
                    if (   cbEA >= RT_UOFFSETOF(UDFEADATAFILETIMES, aTimestamps) + sizeof(UDFTIMESTAMP)
                        && (pGenEA->u.FileTimes.fFlags & UDF_FILE_TIMES_EA_F_BIRTH)
                        && pGenEA->u.FileTimes.cbTimestamps >= sizeof(UDFTIMESTAMP) )
                        RTFsUdfHlpTimestamp2TimeSpec(&pCore->BirthTime, &pGenEA->u.FileTimes.aTimestamps[0]);
                    break;
            }
        }
        else
            LogRelMax(45, ("rtFsIsoCore_InitFromUdfIcbFileEntry: Warning! Bad ExtAttrib tag/crc: %Rrc\n", rc));
    }

    /*
     * Convert the file mode.
     */
    int rc = RTFsUdfHlpIcbStuffToFileMode(pFileEntry->IcbTag.fFlags, pFileEntry->IcbTag.bFileType,
                                          pFileEntry->fPermissions, &pCore->fAttrib);
    if (RT_SUCCESS(rc))
    {
        /*
         * Convert extent info.
         */
        rc = RTFsUdfHlpGatherExtentsFromIcb(pVolInfo,
                                            &pFileEntry->abExtAttribs[pFileEntry->cbExtAttribs],
                                            pFileEntry->cbAllocDescs,
                                            pFileEntry->IcbTag.fFlags,
                                            idxDefaultPart,
                                              ((uint64_t)pFileEntry->Tag.offTag << pVolInfo->cShiftBlock)
                                            + RT_UOFFSETOF(UDFFILEENTRY, abExtAttribs)
                                            + pFileEntry->cbExtAttribs,
                                            pCore->cbObject,
                                            RT_FROM_MEMBER(pVolInfo, RTFSISOVOL, Udf.VolInfo)->hVfsBacking,
                                            (uint8_t *)pFileEntry,
                                            &pCore->cExtents,
                                            &pCore->FirstExtent,
                                            &pCore->paExtents);
        if (RT_SUCCESS(rc))
        {
            /*
             * We're good.
             */
            return VINF_SUCCESS;
        }
    }
    return rc;
}


/**
 * Initialize a core structure from an UDF ICB range and optionally a file ID.
 *
 * @returns IPRT status code.
 * @param   pCore               The core structure to initialize.
 *                              Caller must've ZEROed this structure!
 * @param   pAllocDesc          The ICB allocation descriptor.
 * @param   pFid                The file ID descriptor.  Optional.
 * @param   offInDir            The offset of the file ID descriptor in the
 *                              parent directory.  This is used when looking up
 *                              shared directory objects.  (Pass 0 for root.)
 * @param   pVol                The instance.
 *
 * @note    Caller must check for UDF_FILE_FLAGS_DELETED before calling if the
 *          object is supposed to be used for real stuff.
 */
static int rtFsIsoCore_InitFromUdfIcbAndFileIdDesc(PRTFSISOCORE pCore, PCUDFLONGAD pAllocDesc,
                                                   PCUDFFILEIDDESC pFid, uintptr_t offInDir, PRTFSISOVOL pVol)
{
    Assert(pCore->cRefs == 0);
    Assert(pCore->cExtents == 0);
    Assert(pCore->paExtents == NULL);
    Assert(pCore->pVol == NULL);

    /*
     * Some size sanity checking.
     */
    if (pAllocDesc->cb <= _64K) /** @todo this isn't necessarily sane... */
    {
        if (pAllocDesc->cb >= sizeof(UDFICBHDR))
        { /* likely */ }
        else
        {
            Log(("rtFsIsoCore_InitFromUdfIcbAndFileIdDesc: ICB too small: %#04x:%010RX32 LB %#x\n",
                 pAllocDesc->Location.uPartitionNo, pAllocDesc->Location.off, pAllocDesc->cb));
            return VERR_ISOFS_ICB_TOO_SMALL;
        }
    }
    else
    {
        Log(("rtFsIsoCore_InitFromUdfIcbAndFileIdDesc: ICB too big: %#04x:%010RX32 LB %#x\n",
             pAllocDesc->Location.uPartitionNo, pAllocDesc->Location.off, pAllocDesc->cb));
        return VERR_ISOFS_ICB_TOO_BIG;
    }

    /*
     * Allocate a temporary buffer, one logical block in size.
     */
    uint8_t * const pbBuf = (uint8_t *)RTMemTmpAlloc(pVol->Udf.VolInfo.cbBlock);
    if (pbBuf)
    {
        uint32_t cProcessed = 0;
        uint32_t cIndirections = 0;
        int rc = RTFsUdfReadIcbRecursive(&pVol->Udf.VolInfo, pVol->hVfsBacking, pbBuf, 0 /*cNestings*/,
                                         rtFsIsoCore_InitFromUdfIcbFileEntry, rtFsIsoCore_InitFromUdfIcbExFileEntry, pCore,
                                         &cProcessed, &cIndirections, *pAllocDesc);
        RTMemTmpFree(pbBuf);
        if (RT_SUCCESS(rc))
        {
            if (cProcessed > 0)
            {
                if (pFid)
                {
                    if (pFid->fFlags & UDF_FILE_FLAGS_HIDDEN)
                        pCore->fAttrib |= RTFS_DOS_HIDDEN;
                    if (pFid->fFlags & UDF_FILE_FLAGS_DELETED)
                        pCore->fAttrib = (pCore->fAttrib & ~RTFS_TYPE_MASK) | RTFS_TYPE_WHITEOUT;
                }

                pCore->cRefs     = 1;
                pCore->pVol      = pVol;
                pCore->offDirRec = offInDir;
                return VINF_SUCCESS;
            }
            rc = VERR_ISOFS_NO_DIRECT_ICB_ENTRIES;
        }

        /* White-out fix. Caller must be checking for UDF_FILE_FLAGS_DELETED */
        if (   pFid
            && (pFid->fFlags & UDF_FILE_FLAGS_DELETED))
        {
            pCore->fAttrib = (pCore->fAttrib & ~RTFS_TYPE_MASK) | RTFS_TYPE_WHITEOUT;
            return VINF_SUCCESS;
        }
        return rc;
    }

    pCore->pVol = NULL;
    return VERR_NO_TMP_MEMORY;
}


/**
 * Simple UDF read function.
 *
 * This deals with extent mappings as well as virtual partition related block
 * mapping and such.
 *
 * @returns VBox status code.
 * @param   pCore           The core object to read data from.
 * @param   offRead         The offset to start reading at.
 * @param   pvBuf           The output buffer.
 * @param   cbToRead        The number of bytes to read.
 * @param   pcbRead         Where to return the number of bytes read.
 * @param   poffPosMov      Where to return the number of bytes to move the read
 *                          position.  Optional.  (Essentially same as pcbRead
 *                          except without the behavior change.)
 */
static int rtFsIsoCore_ReadWorker(PRTFSISOCORE pCore, uint64_t offRead, void *pvBuf, size_t cbToRead,
                                  size_t *pcbRead, size_t *poffPosMov)
{
    /*
     * Check for EOF.
     */
    if (offRead >= pCore->cbObject)
    {
        if (poffPosMov)
            *poffPosMov = 0;
        if (pcbRead)
        {
            *pcbRead = 0;
            return VINF_EOF;
        }
        return VERR_EOF;
    }
    int rcRet = VINF_SUCCESS;
    if (   cbToRead           > pCore->cbObject
        || offRead + cbToRead > pCore->cbObject)
    {
        if (!pcbRead)
        {
            if (poffPosMov)
                *poffPosMov = 0;
            return VERR_EOF;
        }
        cbToRead = pCore->cbObject - offRead;
        rcRet = VINF_EOF;
    }

    uint64_t cbActual = 0;

    /*
     * Don't bother looking up the extent if we're not going to
     * read anything from it.
     */
    if (cbToRead > 0)
    {
        /*
         * Locate the first extent.
         */
        uint64_t        offExtent  = 0;
        uint32_t        iExtent    = 0;
        PCRTFSISOEXTENT pCurExtent = &pCore->FirstExtent;
        if (offRead < pCurExtent->cbExtent)
        { /* likely */ }
        else
            do
            {
                offExtent += pCurExtent->cbExtent;
                pCurExtent = &pCore->paExtents[iExtent++];
                if (iExtent >= pCore->cExtents)
                {
                    memset(pvBuf, 0, cbToRead);

                    if (pcbRead)
                        *pcbRead = cbToRead;
                    if (poffPosMov)
                        *poffPosMov = cbToRead;
                    return rcRet;
                }
            } while (offExtent < offRead);
        Assert(offRead - offExtent < pCurExtent->cbExtent);

        /*
         * Do the reading part.
         */
        PRTFSISOVOL pVol = pCore->pVol;
        for (;;)
        {
            uint64_t offIntoExtent = offRead - offExtent;
            size_t   cbThisRead = pCurExtent->cbExtent - offIntoExtent;
            if (cbThisRead > cbToRead)
                cbThisRead = cbToRead;

            if (pCurExtent->off == UINT64_MAX)
                RT_BZERO(pvBuf, cbThisRead);
            else
            {
                int rc2;
                if (pCurExtent->idxPart == UINT32_MAX)
                    rc2 = RTVfsFileReadAt(pVol->hVfsBacking, pCurExtent->off + offIntoExtent, pvBuf, cbThisRead, NULL);
                else
                {
                    Assert(pVol->enmType == RTFSISOVOLTYPE_UDF);
                    if (pCurExtent->idxPart < pVol->Udf.VolInfo.cPartitions)
                    {
                        PRTFSISOVOLUDFPMAP pPart = &pVol->Udf.VolInfo.paPartitions[pCurExtent->idxPart];
                        switch (pPart->bType)
                        {
                            case RTFSUDF_PMAP_T_PLAIN:
                                rc2 = RTVfsFileReadAt(pVol->hVfsBacking, pPart->offByteLocation + pCurExtent->off + offIntoExtent,
                                                      pvBuf, cbThisRead, NULL);
                                break;

                            default:
                                AssertFailed();
                                rc2 = VERR_ISOFS_IPE_1;
                                break;
                        }
                    }
                    else
                    {
                        Log(("ISO/UDF: Invalid partition index %#x (offset %#RX64), max partitions %#x; iExtent=%#x\n",
                             pCurExtent->idxPart, pCurExtent->off + offIntoExtent, pVol->Udf.VolInfo.cPartitions, iExtent));
                        rc2 = VERR_ISOFS_INVALID_PARTITION_INDEX;
                    }
                }
                if (RT_FAILURE(rc2))
                {
                    rcRet = rc2;
                    break;
                }
            }

            /*
             * Advance the buffer position and check if we're done (probable).
             */
            cbActual += cbThisRead;
            cbToRead -= cbThisRead;
            if (!cbToRead)
                break;
            pvBuf = (uint8_t *)pvBuf + cbThisRead;

            /*
             * Advance to the next extent.
             */
            offExtent += pCurExtent->cbExtent;
            pCurExtent = &pCore->paExtents[iExtent++];
            if (iExtent >= pCore->cExtents)
            {
                memset(pvBuf, 0, cbToRead);
                cbActual += cbToRead;
                break;
            }
        }
    }
    else
        Assert(rcRet == VINF_SUCCESS);

    if (poffPosMov)
        *poffPosMov = cbActual;
    if (pcbRead)
        *pcbRead = cbActual;
    return rcRet;
}


/**
 * Worker for rtFsIsoFile_QueryInfo and rtFsIsoDir_QueryInfo.
 */
static int rtFsIsoCore_QueryInfo(PRTFSISOCORE pCore, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    pObjInfo->cbObject              = pCore->cbObject;
    pObjInfo->cbAllocated           = RT_ALIGN_64(pCore->cbObject, pCore->pVol->cbBlock);
    pObjInfo->AccessTime            = pCore->AccessTime;
    pObjInfo->ModificationTime      = pCore->ModificationTime;
    pObjInfo->ChangeTime            = pCore->ChangeTime;
    pObjInfo->BirthTime             = pCore->BirthTime;
    pObjInfo->Attr.fMode            = pCore->fAttrib;
    pObjInfo->Attr.enmAdditional    = enmAddAttr;

    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING: RT_FALL_THRU();
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.u.Unix.uid           = NIL_RTUID;
            pObjInfo->Attr.u.Unix.gid           = NIL_RTGID;
            pObjInfo->Attr.u.Unix.cHardlinks    = 1;
            pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
            pObjInfo->Attr.u.Unix.INodeId       = pCore->idINode;
            pObjInfo->Attr.u.Unix.fFlags        = 0;
            pObjInfo->Attr.u.Unix.GenerationId  = pCore->uVersion;
            pObjInfo->Attr.u.Unix.Device        = 0;
            break;
        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.u.UnixOwner.uid       = 0;
            pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            break;
        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid       = 0;
            pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            break;
        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.u.EASize.cb = 0;
            break;
        default:
            return VERR_INVALID_PARAMETER;
    }

    if (   pCore->fHaveRockInfo
        && enmAddAttr != RTFSOBJATTRADD_NOTHING)
    {
        /** @todo Read the the rock info for this entry. */
    }

    return VINF_SUCCESS;
}


/**
 * Worker for rtFsIsoFile_Close and rtFsIsoDir_Close that does common work.
 *
 * @param   pCore           The common shared structure.
 */
static void rtFsIsoCore_Destroy(PRTFSISOCORE pCore)
{
    if (pCore->pParentDir)
        rtFsIsoDirShrd_RemoveOpenChild(pCore->pParentDir, pCore);
    if (pCore->paExtents)
    {
        RTMemFree(pCore->paExtents);
        pCore->paExtents = NULL;
    }
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtFsIsoFile_Close(void *pvThis)
{
    PRTFSISOFILEOBJ  pThis   = (PRTFSISOFILEOBJ)pvThis;
    LogFlow(("rtFsIsoFile_Close(%p/%p)\n", pThis, pThis->pShared));

    PRTFSISOFILESHRD pShared = pThis->pShared;
    pThis->pShared = NULL;
    if (pShared)
    {
        if (ASMAtomicDecU32(&pShared->Core.cRefs) == 0)
        {
            LogFlow(("rtFsIsoFile_Close: Destroying shared structure %p\n", pShared));
            rtFsIsoCore_Destroy(&pShared->Core);
            RTMemFree(pShared);
        }
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsIsoFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSISOFILEOBJ pThis = (PRTFSISOFILEOBJ)pvThis;
    return rtFsIsoCore_QueryInfo(&pThis->pShared->Core, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtFsIsoFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTFSISOFILEOBJ  pThis   = (PRTFSISOFILEOBJ)pvThis;
    PRTFSISOFILESHRD pShared = pThis->pShared;
    AssertReturn(pSgBuf->cSegs == 1, VERR_INTERNAL_ERROR_3);
    RT_NOREF(fBlocking);

#if 1
    /* Apply default offset. */
    if (off == -1)
        off = pThis->offFile;
    else
        AssertReturn(off >= 0, VERR_INTERNAL_ERROR_3);

    /* Do the read. */
    size_t offDelta = 0;
    int rc = rtFsIsoCore_ReadWorker(&pShared->Core, off, (uint8_t *)pSgBuf->paSegs[0].pvSeg,
                                    pSgBuf->paSegs[0].cbSeg, pcbRead, &offDelta);

    /* Update the file position and return. */
    pThis->offFile = off + offDelta;
    RTSgBufAdvance(pSgBuf, offDelta);

    return rc;
#else


    /*
     * Check for EOF.
     */
    if (off == -1)
        off = pThis->offFile;
    if ((uint64_t)off >= pShared->Core.cbObject)
    {
        if (pcbRead)
        {
            *pcbRead = 0;
            return VINF_EOF;
        }
        return VERR_EOF;
    }

    if (pShared->Core.pVol->enmType == RTFSISOVOLTYPE_UDF)
    {
        return VERR_ISOFS_UDF_NOT_IMPLEMENTED;
    }

    /*
     * Simple case: File has a single extent.
     */
    int      rc         = VINF_SUCCESS;
    size_t   cbRead     = 0;
    uint64_t cbFileLeft = pShared->Core.cbObject - (uint64_t)off;
    size_t   cbLeft     = pSgBuf->paSegs[0].cbSeg;
    uint8_t *pbDst      = (uint8_t *)pSgBuf->paSegs[0].pvSeg;
    if (pShared->Core.cExtents == 1)
    {
        if (cbLeft > 0)
        {
            size_t cbToRead = cbLeft;
            if (cbToRead > cbFileLeft)
                cbToRead = (size_t)cbFileLeft;
            rc = RTVfsFileReadAt(pShared->Core.pVol->hVfsBacking, pShared->Core.FirstExtent.off + off, pbDst, cbToRead, NULL);
            if (RT_SUCCESS(rc))
            {
                off         += cbToRead;
                pbDst       += cbToRead;
                cbRead      += cbToRead;
                cbFileLeft  -= cbToRead;
                cbLeft      -= cbToRead;
            }
        }
    }
    /*
     * Complicated case: Work the file content extent by extent.
     */
    else
    {
        return VERR_NOT_IMPLEMENTED; /** @todo multi-extent stuff . */
    }

    /* Update the offset and return. */
    pThis->offFile = off;
    if (pcbRead)
        *pcbRead = cbRead;
    return VINF_SUCCESS;
#endif
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtFsIsoFile_Flush(void *pvThis)
{
    RT_NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtFsIsoFile_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                                 uint32_t *pfRetEvents)
{
    NOREF(pvThis);
    int rc;
    if (fEvents != RTPOLL_EVT_ERROR)
    {
        *pfRetEvents = fEvents & ~RTPOLL_EVT_ERROR;
        rc = VINF_SUCCESS;
    }
    else if (fIntr)
        rc = RTThreadSleep(cMillies);
    else
    {
        uint64_t uMsStart = RTTimeMilliTS();
        do
            rc = RTThreadSleep(cMillies);
        while (   rc == VERR_INTERRUPTED
               && RTTimeMilliTS() - uMsStart < cMillies);
        if (rc == VERR_INTERRUPTED)
            rc = VERR_TIMEOUT;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtFsIsoFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTFSISOFILEOBJ pThis = (PRTFSISOFILEOBJ)pvThis;
    *poffActual = pThis->offFile;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtFsIsoFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTFSISOFILEOBJ pThis = (PRTFSISOFILEOBJ)pvThis;
    RTFOFF offNew;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offNew = offSeek;
            break;
        case RTFILE_SEEK_END:
            offNew = (RTFOFF)pThis->pShared->Core.cbObject + offSeek;
            break;
        case RTFILE_SEEK_CURRENT:
            offNew = (RTFOFF)pThis->offFile + offSeek;
            break;
        default:
            return VERR_INVALID_PARAMETER;
    }
    if (offNew >= 0)
    {
        pThis->offFile = offNew;
        *poffActual    = offNew;
        return VINF_SUCCESS;
    }
    return VERR_NEGATIVE_SEEK;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtFsIsoFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTFSISOFILEOBJ pThis = (PRTFSISOFILEOBJ)pvThis;
    *pcbFile = pThis->pShared->Core.cbObject;
    return VINF_SUCCESS;
}


/**
 * ISO FS file operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_rtFsIsoFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "FatFile",
            rtFsIsoFile_Close,
            rtFsIsoFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtFsIsoFile_Read,
        NULL /*Write*/,
        rtFsIsoFile_Flush,
        rtFsIsoFile_PollOne,
        rtFsIsoFile_Tell,
        NULL /*pfnSkip*/,
        NULL /*pfnZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        NULL /*SetMode*/,
        NULL /*SetTimes*/,
        NULL /*SetOwner*/,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsIsoFile_Seek,
    rtFsIsoFile_QuerySize,
    NULL /*SetSize*/,
    NULL /*QueryMaxSize*/,
    RTVFSFILEOPS_VERSION
};


/**
 * Instantiates a new file, from ISO 9660 info.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO volume instance.
 * @param   pParentDir      The parent directory (shared part).
 * @param   pDirRec         The directory record.
 * @param   cDirRecs        Number of directory records if more than one.
 * @param   offDirRec       The byte offset of the directory record.
 * @param   offEntryInDir   The byte offset of the directory entry in the parent
 *                          directory.
 * @param   fOpen           RTFILE_O_XXX flags.
 * @param   uVersion        The file version number (since the caller already
 *                          parsed the filename, we don't want to repeat the
 *                          effort here).
 * @param   pRockInfo       Optional rock ridge info for the file.
 * @param   phVfsFile       Where to return the file handle.
 */
static int rtFsIsoFile_New9660(PRTFSISOVOL pThis, PRTFSISODIRSHRD pParentDir, PCISO9660DIRREC pDirRec, uint32_t cDirRecs,
                               uint64_t offDirRec, uint64_t fOpen, uint32_t uVersion, PCRTFSISOROCKINFO pRockInfo,
                               PRTVFSFILE phVfsFile)
{
    AssertPtr(pParentDir);

    /*
     * Create a VFS object.
     */
    PRTFSISOFILEOBJ pNewFile;
    int rc = RTVfsNewFile(&g_rtFsIsoFileOps, sizeof(*pNewFile), fOpen, pThis->hVfsSelf, NIL_RTVFSLOCK /*use volume lock*/,
                          phVfsFile, (void **)&pNewFile);
    if (RT_SUCCESS(rc))
    {
        /*
         * Look for existing shared object, create a new one if necessary.
         */
        PRTFSISOFILESHRD pShared = (PRTFSISOFILESHRD)rtFsIsoDir_LookupShared(pParentDir, offDirRec);
        if (pShared)
        {
            LogFlow(("rtFsIsoFile_New9660: cbObject=%#RX64 First Extent: off=%#RX64 cb=%#RX64\n",
                     pShared->Core.cbObject, pShared->Core.FirstExtent.off, pShared->Core.FirstExtent.cbExtent));
            pNewFile->offFile = 0;
            pNewFile->pShared = pShared;
            return VINF_SUCCESS;
        }

        pShared = (PRTFSISOFILESHRD)RTMemAllocZ(sizeof(*pShared));
        if (pShared)
        {
            rc = rtFsIsoCore_InitFrom9660DirRec(&pShared->Core, pDirRec, cDirRecs, offDirRec, uVersion, pRockInfo, pThis);
            if (RT_SUCCESS(rc))
            {
                rtFsIsoDirShrd_AddOpenChild(pParentDir, &pShared->Core);
                LogFlow(("rtFsIsoFile_New9660: cbObject=%#RX64 First Extent: off=%#RX64 cb=%#RX64\n",
                         pShared->Core.cbObject, pShared->Core.FirstExtent.off, pShared->Core.FirstExtent.cbExtent));
                pNewFile->offFile = 0;
                pNewFile->pShared = pShared;
                return VINF_SUCCESS;
            }
            RTMemFree(pShared);
        }
        else
            rc = VERR_NO_MEMORY;

        /* Destroy the file object. */
        pNewFile->offFile = 0;
        pNewFile->pShared = NULL;
        RTVfsFileRelease(*phVfsFile);
    }
    *phVfsFile = NIL_RTVFSFILE;
    return rc;
}


/**
 * Instantiates a new file, from UDF info.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO volume instance.
 * @param   pParentDir      The parent directory (shared part).
 * @param   pFid            The file ID descriptor.  (Points to parent directory
 *                          content.)
 * @param   fOpen           RTFILE_O_XXX flags.
 * @param   phVfsFile       Where to return the file handle.
 */
static int rtFsIsoFile_NewUdf(PRTFSISOVOL pThis, PRTFSISODIRSHRD pParentDir, PCUDFFILEIDDESC pFid,
                              uint64_t fOpen, PRTVFSFILE phVfsFile)
{
    AssertPtr(pParentDir);
    uintptr_t const offInDir = (uintptr_t)pFid - (uintptr_t)pParentDir->pbDir;
    Assert(offInDir < pParentDir->cbDir);
    Assert(!(pFid->fFlags & UDF_FILE_FLAGS_DELETED));
    Assert(!(pFid->fFlags & UDF_FILE_FLAGS_DIRECTORY));

    /*
     * Create a VFS object.
     */
    PRTFSISOFILEOBJ pNewFile;
    int rc = RTVfsNewFile(&g_rtFsIsoFileOps, sizeof(*pNewFile), fOpen, pThis->hVfsSelf, NIL_RTVFSLOCK /*use volume lock*/,
                          phVfsFile, (void **)&pNewFile);
    if (RT_SUCCESS(rc))
    {
        /*
         * Look for existing shared object.  Make sure it's a file.
         */
        PRTFSISOFILESHRD pShared = (PRTFSISOFILESHRD)rtFsIsoDir_LookupShared(pParentDir, offInDir);
        if (pShared)
        {
            if (!RTFS_IS_FILE(pShared->Core.fAttrib))
            {
                LogFlow(("rtFsIsoFile_NewUdf: cbObject=%#RX64 First Extent: off=%#RX64 cb=%#RX64\n",
                         pShared->Core.cbObject, pShared->Core.FirstExtent.off, pShared->Core.FirstExtent.cbExtent));
                pNewFile->offFile = 0;
                pNewFile->pShared = pShared;
                return VINF_SUCCESS;
            }
            rc = VERR_NOT_A_FILE;
        }
        /*
         * Create a shared object for this alleged file.
         */
        else
        {
            pShared = (PRTFSISOFILESHRD)RTMemAllocZ(sizeof(*pShared));
            if (pShared)
            {
                rc = rtFsIsoCore_InitFromUdfIcbAndFileIdDesc(&pShared->Core, &pFid->Icb, pFid, offInDir, pThis);
                if (RT_SUCCESS(rc))
                {
                    if (RTFS_IS_FILE(pShared->Core.fAttrib))
                    {
                        rtFsIsoDirShrd_AddOpenChild(pParentDir, &pShared->Core);

                        LogFlow(("rtFsIsoFile_NewUdf: cbObject=%#RX64 First Extent: off=%#RX64 cb=%#RX64\n",
                                 pShared->Core.cbObject, pShared->Core.FirstExtent.off, pShared->Core.FirstExtent.cbExtent));
                        pNewFile->offFile = 0;
                        pNewFile->pShared = pShared;
                        return VINF_SUCCESS;
                    }
                    rc = VERR_NOT_A_FILE;
                    rtFsIsoCore_Destroy(&pShared->Core);
                }
                RTMemFree(pShared);
            }
            else
                rc = VERR_NO_MEMORY;
        }

        /* Destroy the file object. */
        pNewFile->offFile = 0;
        pNewFile->pShared = NULL;
        RTVfsFileRelease(*phVfsFile);
    }
    *phVfsFile = NIL_RTVFSFILE;
    return rc;
}


/**
 * Looks up the shared structure for a child.
 *
 * @returns Referenced pointer to the shared structure, NULL if not found.
 * @param   pThis           The directory.
 * @param   offDirRec       The directory record offset of the child.
 */
static PRTFSISOCORE rtFsIsoDir_LookupShared(PRTFSISODIRSHRD pThis, uint64_t offDirRec)
{
    PRTFSISOCORE pCur;
    RTListForEach(&pThis->OpenChildren, pCur, RTFSISOCORE, Entry)
    {
        if (pCur->offDirRec == offDirRec)
        {
            uint32_t cRefs = ASMAtomicIncU32(&pCur->cRefs);
            Assert(cRefs > 1); RT_NOREF(cRefs);
            return pCur;
        }
    }
    return NULL;
}


#ifdef RT_STRICT
/**
 * Checks if @a pNext is an extent of @a pFirst.
 *
 * @returns true if @a pNext is the next extent, false if not
 * @param   pFirst      The directory record describing the first or the
 *                      previous extent.
 * @param   pNext       The directory record alleged to be the next extent.
 */
DECLINLINE(bool) rtFsIsoDir_Is9660DirRecNextExtent(PCISO9660DIRREC pFirst, PCISO9660DIRREC pNext)
{
    if (RT_LIKELY(pNext->bFileIdLength == pFirst->bFileIdLength))
    {
        if (RT_LIKELY((pNext->fFileFlags | ISO9660_FILE_FLAGS_MULTI_EXTENT) == pFirst->fFileFlags))
        {
            if (RT_LIKELY(memcmp(pNext->achFileId, pFirst->achFileId, pNext->bFileIdLength) == 0))
                return true;
        }
    }
    return false;
}
#endif /* RT_STRICT */


/**
 * Parses rock ridge information if present in the directory entry.
 *
 * @param   pVol                The volume structure.
 * @param   pParseInfo          Parse info and output.
 * @param   pbSys               The system area of the directory record.
 * @param   cbSys               The number of bytes present in the sys area.
 * @param   fIsFirstDirRec      Set if this is the '.' directory entry in the
 *                              root directory.  (Some entries applies only to
 *                              it.)
 * @param   fContinuationRecord Set if we're processing a continuation record in
 *                              living in the abRockBuf.
 */
static void rtFsIsoDirShrd_ParseRockRidgeData(PRTFSISOVOL pVol, PRTFSISOROCKINFO pParseInfo, uint8_t const *pbSys,
                                              size_t cbSys, bool fIsFirstDirRec, bool fContinuationRecord)
{
    while (cbSys >= 4)
    {
        /*
         * Check header length and advance the sys variables.
         */
        PCISO9660SUSPUNION pUnion = (PCISO9660SUSPUNION)pbSys;
        if (   pUnion->Hdr.cbEntry > cbSys
            || pUnion->Hdr.cbEntry < sizeof(pUnion->Hdr))
        {
            Log4(("rtFsIsoDir_ParseRockRidgeData: cbEntry=%#x cbSys=%#x (%#x %#x)\n",
                  pUnion->Hdr.cbEntry, cbSys, pUnion->Hdr.bSig1, pUnion->Hdr.bSig2));
            break;
        }
        pbSys += pUnion->Hdr.cbEntry;
        cbSys -= pUnion->Hdr.cbEntry;

        /*
         * Process fields.
         */
        uint16_t const uSig = SUSP_MAKE_SIG(pUnion->Hdr.bSig1, pUnion->Hdr.bSig2);
        switch (uSig)
        {
            /*
             * System use sharing protocol entries.
             */
            case SUSP_MAKE_SIG(ISO9660SUSPCE_SIG1, ISO9660SUSPCE_SIG2):
            {
                if (RT_BE2H_U32(pUnion->CE.offBlock.be) != RT_LE2H_U32(pUnion->CE.offBlock.le))
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Invalid CE offBlock field: be=%#x vs le=%#x\n",
                          RT_BE2H_U32(pUnion->CE.offBlock.be), RT_LE2H_U32(pUnion->CE.offBlock.le)));
                else if (RT_BE2H_U32(pUnion->CE.cbData.be) != RT_LE2H_U32(pUnion->CE.cbData.le))
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Invalid CE cbData field: be=%#x vs le=%#x\n",
                          RT_BE2H_U32(pUnion->CE.cbData.be), RT_LE2H_U32(pUnion->CE.cbData.le)));
                else if (RT_BE2H_U32(pUnion->CE.offData.be) != RT_LE2H_U32(pUnion->CE.offData.le))
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Invalid CE offData field: be=%#x vs le=%#x\n",
                          RT_BE2H_U32(pUnion->CE.offData.be), RT_LE2H_U32(pUnion->CE.offData.le)));
                else if (!fContinuationRecord)
                {
                    uint64_t offData = ISO9660_GET_ENDIAN(&pUnion->CE.offBlock) * (uint64_t)ISO9660_SECTOR_SIZE;
                    offData += ISO9660_GET_ENDIAN(&pUnion->CE.offData);
                    uint32_t cbData  = ISO9660_GET_ENDIAN(&pUnion->CE.cbData);
                    if (cbData <= sizeof(pVol->abRockBuf) - (uint32_t)(offData & ISO9660_SECTOR_OFFSET_MASK))
                    {
                        RTCritSectEnter(&pVol->RockBufLock);

                        AssertCompile(sizeof(pVol->abRockBuf) == ISO9660_SECTOR_SIZE);
                        uint64_t offDataBlock = offData & ~(uint64_t)ISO9660_SECTOR_OFFSET_MASK;
                        if (pVol->offRockBuf == offDataBlock)
                            rtFsIsoDirShrd_ParseRockRidgeData(pVol, pParseInfo,
                                                              &pVol->abRockBuf[offData & ISO9660_SECTOR_OFFSET_MASK],
                                                              cbData, fIsFirstDirRec, true /*fContinuationRecord*/);
                        else
                        {
                            int rc = RTVfsFileReadAt(pVol->hVfsBacking, offDataBlock,
                                                     pVol->abRockBuf, sizeof(pVol->abRockBuf), NULL);
                            if (RT_SUCCESS(rc))
                                rtFsIsoDirShrd_ParseRockRidgeData(pVol, pParseInfo,
                                                                  &pVol->abRockBuf[offData & ISO9660_SECTOR_OFFSET_MASK],
                                                                  cbData, fIsFirstDirRec, true /*fContinuationRecord*/);
                            else
                                Log4(("rtFsIsoDir_ParseRockRidgeData: Error reading continuation record at %#RX64: %Rrc\n",
                                      offDataBlock, rc));
                        }

                        RTCritSectLeave(&pVol->RockBufLock);
                    }
                    else
                        Log4(("rtFsIsoDir_ParseRockRidgeData: continuation record isn't within a sector! offData=%#RX64 cbData=%#RX32\n",
                              cbData, offData));
                }
                else
                    Log4(("rtFsIsoDir_ParseRockRidgeData: nested continuation record!\n"));
                break;
            }

            case SUSP_MAKE_SIG(ISO9660SUSPSP_SIG1, ISO9660SUSPSP_SIG2): /* SP */
                if (   pUnion->Hdr.cbEntry  != ISO9660SUSPSP_LEN
                    || pUnion->Hdr.bVersion != ISO9660SUSPSP_VER
                    || pUnion->SP.bCheck1   != ISO9660SUSPSP_CHECK1
                    || pUnion->SP.bCheck2   != ISO9660SUSPSP_CHECK2
                    || pUnion->SP.cbSkip > UINT8_MAX - RT_UOFFSETOF(ISO9660DIRREC, achFileId[1]))
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'SP' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x), bCheck1=%#x (vs %#x), bCheck2=%#x (vs %#x), cbSkip=%#x (vs max %#x)\n",
                          pUnion->Hdr.cbEntry, ISO9660SUSPSP_LEN, pUnion->Hdr.bVersion, ISO9660SUSPSP_VER,
                          pUnion->SP.bCheck1, ISO9660SUSPSP_CHECK1, pUnion->SP.bCheck2, ISO9660SUSPSP_CHECK2,
                          pUnion->SP.cbSkip, UINT8_MAX - RT_UOFFSETOF(ISO9660DIRREC, achFileId[1]) ));
                else if (!fIsFirstDirRec)
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Ignorining 'SP' entry in non-root directory record\n"));
                else if (pParseInfo->fSuspSeenSP)
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Ignorining additional 'SP' entry\n"));
                else
                {
                    pVol->offSuspSkip = pUnion->SP.cbSkip;
                    if (pUnion->SP.cbSkip != 0)
                        Log4(("rtFsIsoDir_ParseRockRidgeData: SP: cbSkip=%#x\n", pUnion->SP.cbSkip));
                }
                break;

            case SUSP_MAKE_SIG(ISO9660SUSPER_SIG1, ISO9660SUSPER_SIG2): /* ER */
                if (   pUnion->Hdr.cbEntry >   RT_UOFFSETOF(ISO9660SUSPER, achPayload) + (uint32_t)pUnion->ER.cchIdentifier
                                             + (uint32_t)pUnion->ER.cchDescription     + (uint32_t)pUnion->ER.cchSource
                    || pUnion->Hdr.bVersion != ISO9660SUSPER_VER)
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'ER' entry: cbEntry=%#x bVersion=%#x (vs %#x) cchIdentifier=%#x cchDescription=%#x cchSource=%#x\n",
                          pUnion->Hdr.cbEntry, pUnion->Hdr.bVersion, ISO9660SUSPER_VER, pUnion->ER.cchIdentifier,
                          pUnion->ER.cchDescription, pUnion->ER.cchSource));
                else if (!fIsFirstDirRec)
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Ignorining 'ER' entry in non-root directory record\n"));
                else if (   pUnion->ER.bVersion == 1 /* RRIP detection */
                         && (   (pUnion->ER.cchIdentifier >= 4  && strncmp(pUnion->ER.achPayload, ISO9660_RRIP_ID, 4 /*RRIP*/) == 0)
                             || (pUnion->ER.cchIdentifier >= 10 && strncmp(pUnion->ER.achPayload, RT_STR_TUPLE(ISO9660_RRIP_1_12_ID)) == 0) ))
                {
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Rock Ridge 'ER' entry: v%u id='%.*s' desc='%.*s' source='%.*s'\n",
                          pUnion->ER.bVersion, pUnion->ER.cchIdentifier, pUnion->ER.achPayload,
                          pUnion->ER.cchDescription, &pUnion->ER.achPayload[pUnion->ER.cchIdentifier],
                          pUnion->ER.cchSource, &pUnion->ER.achPayload[pUnion->ER.cchIdentifier + pUnion->ER.cchDescription]));
                    pVol->fHaveRock = true;
                    pParseInfo->cRockEntries++;
                }
                else
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Unknown extension in 'ER' entry: v%u id='%.*s' desc='%.*s' source='%.*s'\n",
                          pUnion->ER.bVersion, pUnion->ER.cchIdentifier, pUnion->ER.achPayload,
                          pUnion->ER.cchDescription, &pUnion->ER.achPayload[pUnion->ER.cchIdentifier],
                          pUnion->ER.cchSource, &pUnion->ER.achPayload[pUnion->ER.cchIdentifier + pUnion->ER.cchDescription]));
                break;

            case SUSP_MAKE_SIG(ISO9660SUSPPD_SIG1, ISO9660SUSPPD_SIG2): /* PD - ignored */
            case SUSP_MAKE_SIG(ISO9660SUSPST_SIG1, ISO9660SUSPST_SIG2): /* ST - ignore for now */
            case SUSP_MAKE_SIG(ISO9660SUSPES_SIG1, ISO9660SUSPES_SIG2): /* ES - ignore for now */
                break;

            /*
             * Rock ridge interchange protocol entries.
             */
            case SUSP_MAKE_SIG(ISO9660RRIPRR_SIG1, ISO9660RRIPRR_SIG2): /* RR */
                if (   pUnion->RR.Hdr.cbEntry  != ISO9660RRIPRR_LEN
                    || pUnion->RR.Hdr.bVersion != ISO9660RRIPRR_VER)
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'RR' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x\n",
                          pUnion->RR.Hdr.cbEntry, ISO9660RRIPRR_LEN, pUnion->RR.Hdr.bVersion, ISO9660RRIPRR_VER, pUnion->RR.fFlags));
                else
                    pParseInfo->cRockEntries++; /* otherwise ignored */
                break;

            case SUSP_MAKE_SIG(ISO9660RRIPPX_SIG1, ISO9660RRIPPX_SIG2): /* PX */
                if (   (   pUnion->PX.Hdr.cbEntry  != ISO9660RRIPPX_LEN
                        && pUnion->PX.Hdr.cbEntry  != ISO9660RRIPPX_LEN_NO_INODE)
                    || pUnion->PX.Hdr.bVersion != ISO9660RRIPPX_VER
                    || RT_BE2H_U32(pUnion->PX.fMode.be)      != RT_LE2H_U32(pUnion->PX.fMode.le)
                    || RT_BE2H_U32(pUnion->PX.cHardlinks.be) != RT_LE2H_U32(pUnion->PX.cHardlinks.le)
                    || RT_BE2H_U32(pUnion->PX.uid.be)        != RT_LE2H_U32(pUnion->PX.uid.le)
                    || RT_BE2H_U32(pUnion->PX.gid.be)        != RT_LE2H_U32(pUnion->PX.gid.le)
                    || (   pUnion->PX.Hdr.cbEntry  == ISO9660RRIPPX_LEN
                        && RT_BE2H_U32(pUnion->PX.INode.be)  != RT_LE2H_U32(pUnion->PX.INode.le)) )
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'PX' entry: cbEntry=%#x (vs %#x or %#x), bVersion=%#x (vs %#x) fMode=%#x/%#x cHardlinks=%#x/%#x uid=%#x/%#x gid=%#x/%#x inode=%#x/%#x\n",
                          pUnion->PX.Hdr.cbEntry, ISO9660RRIPPX_LEN, ISO9660RRIPPX_LEN_NO_INODE,
                          pUnion->PX.Hdr.bVersion, ISO9660RRIPPX_VER,
                          RT_BE2H_U32(pUnion->PX.fMode.be),      RT_LE2H_U32(pUnion->PX.fMode.le),
                          RT_BE2H_U32(pUnion->PX.cHardlinks.be), RT_LE2H_U32(pUnion->PX.cHardlinks.le),
                          RT_BE2H_U32(pUnion->PX.uid.be),        RT_LE2H_U32(pUnion->PX.uid.le),
                          RT_BE2H_U32(pUnion->PX.gid.be),        RT_LE2H_U32(pUnion->PX.gid.le),
                          pUnion->PX.Hdr.cbEntry == ISO9660RRIPPX_LEN ? RT_BE2H_U32(pUnion->PX.INode.be) : 0,
                          pUnion->PX.Hdr.cbEntry == ISO9660RRIPPX_LEN ? RT_LE2H_U32(pUnion->PX.INode.le) : 0 ));
                else
                {
                    if (   RTFS_IS_DIRECTORY(ISO9660_GET_ENDIAN(&pUnion->PX.fMode))
                        == RTFS_IS_DIRECTORY(pParseInfo->Info.Attr.fMode))
                        pParseInfo->Info.Attr.fMode = ISO9660_GET_ENDIAN(&pUnion->PX.fMode);
                    else
                        Log4(("rtFsIsoDir_ParseRockRidgeData: 'PX' entry changes directory-ness: fMode=%#x, existing %#x; ignored\n",
                              ISO9660_GET_ENDIAN(&pUnion->PX.fMode), pParseInfo->Info.Attr.fMode));
                    pParseInfo->Info.Attr.u.Unix.cHardlinks = ISO9660_GET_ENDIAN(&pUnion->PX.cHardlinks);
                    pParseInfo->Info.Attr.u.Unix.uid        = ISO9660_GET_ENDIAN(&pUnion->PX.uid);
                    pParseInfo->Info.Attr.u.Unix.gid        = ISO9660_GET_ENDIAN(&pUnion->PX.gid);
                    /* ignore inode */
                    pParseInfo->cRockEntries++;
                }
                break;

            case SUSP_MAKE_SIG(ISO9660RRIPPN_SIG1, ISO9660RRIPPN_SIG2): /* PN */
                if (   pUnion->PN.Hdr.cbEntry  != ISO9660RRIPPN_LEN
                    || pUnion->PN.Hdr.bVersion != ISO9660RRIPPN_VER
                    || RT_BE2H_U32(pUnion->PN.Major.be) != RT_LE2H_U32(pUnion->PN.Major.le)
                    || RT_BE2H_U32(pUnion->PN.Minor.be) != RT_LE2H_U32(pUnion->PN.Minor.le))
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'PN' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) Major=%#x/%#x Minor=%#x/%#x\n",
                          pUnion->PN.Hdr.cbEntry, ISO9660RRIPPN_LEN, pUnion->PN.Hdr.bVersion, ISO9660RRIPPN_VER,
                          RT_BE2H_U32(pUnion->PN.Major.be),      RT_LE2H_U32(pUnion->PN.Major.le),
                          RT_BE2H_U32(pUnion->PN.Minor.be),      RT_LE2H_U32(pUnion->PN.Minor.le) ));
                else if (RTFS_IS_DIRECTORY(pParseInfo->Info.Attr.fMode))
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Ignorning 'PN' entry for directory (%#x/%#x)\n",
                          ISO9660_GET_ENDIAN(&pUnion->PN.Major), ISO9660_GET_ENDIAN(&pUnion->PN.Minor) ));
                else
                {
                    pParseInfo->Info.Attr.u.Unix.Device = RTDEV_MAKE(ISO9660_GET_ENDIAN(&pUnion->PN.Major),
                                                                     ISO9660_GET_ENDIAN(&pUnion->PN.Minor));
                    pParseInfo->cRockEntries++;
                }
                break;

            case SUSP_MAKE_SIG(ISO9660RRIPTF_SIG1, ISO9660RRIPTF_SIG2): /* TF */
                if (   pUnion->TF.Hdr.bVersion != ISO9660RRIPTF_VER
                    || pUnion->TF.Hdr.cbEntry < Iso9660RripTfCalcLength(pUnion->TF.fFlags))
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'TF' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x\n",
                          pUnion->TF.Hdr.cbEntry, Iso9660RripTfCalcLength(pUnion->TF.fFlags),
                          pUnion->TF.Hdr.bVersion, ISO9660RRIPTF_VER, RT_BE2H_U32(pUnion->TF.fFlags) ));
                else if (!(pUnion->TF.fFlags & ISO9660RRIPTF_F_LONG_FORM))
                {
                    PCISO9660RECTIMESTAMP pTimestamp = (PCISO9660RECTIMESTAMP)&pUnion->TF.abPayload[0];
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_BIRTH)
                    {
                        rtFsIso9660DateTime2TimeSpec(&pParseInfo->Info.BirthTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_MODIFY)
                    {
                        rtFsIso9660DateTime2TimeSpec(&pParseInfo->Info.ModificationTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_ACCESS)
                    {
                        rtFsIso9660DateTime2TimeSpec(&pParseInfo->Info.AccessTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_CHANGE)
                    {
                        rtFsIso9660DateTime2TimeSpec(&pParseInfo->Info.ChangeTime, pTimestamp);
                        pTimestamp++;
                    }
                    pParseInfo->cRockEntries++;
                }
                else
                {
                    PCISO9660TIMESTAMP pTimestamp = (PCISO9660TIMESTAMP)&pUnion->TF.abPayload[0];
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_BIRTH)
                    {
                        rtFsIso9660DateTime2TimeSpecIfValid(&pParseInfo->Info.BirthTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_MODIFY)
                    {
                        rtFsIso9660DateTime2TimeSpecIfValid(&pParseInfo->Info.ModificationTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_ACCESS)
                    {
                        rtFsIso9660DateTime2TimeSpecIfValid(&pParseInfo->Info.AccessTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_CHANGE)
                    {
                        rtFsIso9660DateTime2TimeSpecIfValid(&pParseInfo->Info.ChangeTime, pTimestamp);
                        pTimestamp++;
                    }
                    pParseInfo->cRockEntries++;
                }
                break;

            case SUSP_MAKE_SIG(ISO9660RRIPSF_SIG1, ISO9660RRIPSF_SIG2): /* SF */
                Log4(("rtFsIsoDir_ParseRockRidgeData: Sparse file support not yet implemented!\n"));
                break;

            case SUSP_MAKE_SIG(ISO9660RRIPSL_SIG1, ISO9660RRIPSL_SIG2): /* SL */
                if (   pUnion->SL.Hdr.bVersion != ISO9660RRIPSL_VER
                    || pUnion->SL.Hdr.cbEntry < RT_UOFFSETOF(ISO9660RRIPSL, abComponents[2])
                    || (pUnion->SL.fFlags & ~ISO9660RRIP_SL_F_CONTINUE)
                    || (pUnion->SL.abComponents[0] & ISO9660RRIP_SL_C_RESERVED_MASK) )
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'SL' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x comp[0].fFlags=%#x\n",
                          pUnion->SL.Hdr.cbEntry, RT_UOFFSETOF(ISO9660RRIPSL, abComponents[2]),
                          pUnion->SL.Hdr.bVersion, ISO9660RRIPSL_VER, pUnion->SL.fFlags, pUnion->SL.abComponents[0]));
                else if (pParseInfo->fSeenLastSL)
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Unexpected 'SL!' entry\n"));
                else
                {
                    pParseInfo->cRockEntries++;
                    pParseInfo->fSeenLastSL = !(pUnion->SL.fFlags & ISO9660RRIP_SL_F_CONTINUE); /* used in loop */

                    size_t         offDst    = pParseInfo->cchLinkTarget;
                    uint8_t const *pbSrc     = &pUnion->SL.abComponents[0];
                    uint32_t       cbSrcLeft = pUnion->SL.Hdr.cbEntry - RT_UOFFSETOF(ISO9660RRIPSL, abComponents);
                    while (cbSrcLeft >= 2)
                    {
                        uint8_t const  fFlags  = pbSrc[0];
                        uint8_t        cchCopy = pbSrc[1];
                        uint32_t const cbSkip  = cchCopy + 2;
                        if (cbSkip > cbSrcLeft)
                        {
                            Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'SL' component: component flags=%#x, component length+2=%#x vs %#x left\n",
                                  fFlags, cbSkip, cbSrcLeft));
                            break;
                        }

                        const char *pszCopy;
                        switch (fFlags & ~ISO9660RRIP_SL_C_CONTINUE)
                        {
                            case 0:
                                pszCopy = (const char *)&pbSrc[2];
                                break;

                            case ISO9660RRIP_SL_C_CURRENT:
                                if (cchCopy != 0)
                                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'SL' component: CURRENT + %u bytes, ignoring bytes\n", cchCopy));
                                pszCopy = ".";
                                cchCopy = 1;
                                break;

                            case ISO9660RRIP_SL_C_PARENT:
                                if (cchCopy != 0)
                                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'SL' component: PARENT + %u bytes, ignoring bytes\n", cchCopy));
                                pszCopy = "..";
                                cchCopy = 2;
                                break;

                            case ISO9660RRIP_SL_C_ROOT:
                                if (cchCopy != 0)
                                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'SL' component: ROOT + %u bytes, ignoring bytes\n", cchCopy));
                                pszCopy = "/";
                                cchCopy = 1;
                                break;

                            default:
                                Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'SL' component: component flags=%#x (bad), component length=%#x vs %#x left\n",
                                      fFlags, cchCopy, cbSrcLeft));
                                pszCopy = NULL;
                                cchCopy = 0;
                                break;
                        }

                        if (offDst + cchCopy < sizeof(pParseInfo->szLinkTarget))
                        {
                            memcpy(&pParseInfo->szLinkTarget[offDst], pszCopy, cchCopy);
                            offDst += cchCopy;
                        }
                        else
                        {
                            Log4(("rtFsIsoDir_ParseRockRidgeData: 'SL' constructs a too long target! '%.*s%.*s'\n",
                                  offDst, pParseInfo->szLinkTarget, cchCopy, pszCopy));
                            memcpy(&pParseInfo->szLinkTarget[offDst], pszCopy, sizeof(pParseInfo->szLinkTarget) - offDst - 1);
                            offDst = sizeof(pParseInfo->szLinkTarget) - 1;
                            pParseInfo->fOverflowSL = true;
                            break;
                        }

                        /* Advance */
                        pbSrc     += cbSkip;
                        cbSrcLeft -= cbSkip;

                        /* Append slash if appropriate. */
                        if (   !(fFlags & ISO9660RRIP_SL_C_CONTINUE)
                            && (cbSrcLeft >= 2 || !pParseInfo->fSeenLastSL) )
                        {
                            if (offDst + 1 < sizeof(pParseInfo->szLinkTarget))
                                pParseInfo->szLinkTarget[offDst++] = '/';
                            else
                            {
                                Log4(("rtFsIsoDir_ParseRockRidgeData: 'SL' constructs a too long target! '%.*s/'\n",
                                      offDst, pParseInfo->szLinkTarget));
                                pParseInfo->fOverflowSL = true;
                                break;
                            }
                        }
                    }
                    Assert(offDst < sizeof(pParseInfo->szLinkTarget));
                    pParseInfo->szLinkTarget[offDst] = '\0';
                    pParseInfo->cchLinkTarget        = (uint16_t)offDst;
                }
                break;

            case SUSP_MAKE_SIG(ISO9660RRIPNM_SIG1, ISO9660RRIPNM_SIG2): /* NM */
                if (   pUnion->NM.Hdr.bVersion != ISO9660RRIPNM_VER
                    || pUnion->NM.Hdr.cbEntry < RT_UOFFSETOF(ISO9660RRIPNM, achName)
                    || (pUnion->NM.fFlags & ISO9660RRIP_NM_F_RESERVED_MASK) )
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Malformed 'NM' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x %.*Rhxs\n",
                          pUnion->NM.Hdr.cbEntry, RT_UOFFSETOF(ISO9660RRIPNM, achName),
                          pUnion->NM.Hdr.bVersion, ISO9660RRIPNM_VER, pUnion->NM.fFlags,
                          pUnion->NM.Hdr.cbEntry - RT_MIN(pUnion->NM.Hdr.cbEntry, RT_UOFFSETOF(ISO9660RRIPNM, achName)),
                          &pUnion->NM.achName[0] ));
                else if (pParseInfo->fSeenLastNM)
                    Log4(("rtFsIsoDir_ParseRockRidgeData: Unexpected 'NM' entry!\n"));
                else
                {
                    pParseInfo->cRockEntries++;
                    pParseInfo->fSeenLastNM = !(pUnion->NM.fFlags & ISO9660RRIP_NM_F_CONTINUE);

                    uint8_t const cchName = pUnion->NM.Hdr.cbEntry - (uint8_t)RT_UOFFSETOF(ISO9660RRIPNM, achName);
                    if (pUnion->NM.fFlags & (ISO9660RRIP_NM_F_CURRENT | ISO9660RRIP_NM_F_PARENT))
                    {
                        if (cchName == 0 && pParseInfo->szName[0] == '\0')
                            Log4(("rtFsIsoDir_ParseRockRidgeData: Ignoring 'NM' entry for '.' and '..'\n"));
                        else
                            Log4(("rtFsIsoDir_ParseRockRidgeData: Ignoring malformed 'NM' using '.' or '..': fFlags=%#x cchName=%#x %.*Rhxs; szRockNameBuf='%s'\n",
                                  pUnion->NM.fFlags, cchName, cchName, pUnion->NM.achName, pParseInfo->szName));
                        pParseInfo->szName[0]   = '\0';
                        pParseInfo->cchName     = 0;
                        pParseInfo->fSeenLastNM = true;
                    }
                    else
                    {
                        size_t offDst = pParseInfo->cchName;
                        if (offDst + cchName < sizeof(pParseInfo->szName))
                        {
                            memcpy(&pParseInfo->szName[offDst], pUnion->NM.achName, cchName);
                            offDst += cchName;
                            pParseInfo->szName[offDst] = '\0';
                            pParseInfo->cchName        = (uint16_t)offDst;
                        }
                        else
                        {
                            Log4(("rtFsIsoDir_ParseRockRidgeData: 'NM' constructs a too long name, ignoring it all: '%s%.*s'\n",
                                  pParseInfo->szName, cchName, pUnion->NM.achName));
                            pParseInfo->szName[0]   = '\0';
                            pParseInfo->cchName     = 0;
                            pParseInfo->fSeenLastNM = true;
                        }
                    }
                }
                break;

            case SUSP_MAKE_SIG(ISO9660RRIPCL_SIG1, ISO9660RRIPCL_SIG2): /* CL - just warn for now. */
            case SUSP_MAKE_SIG(ISO9660RRIPPL_SIG1, ISO9660RRIPPL_SIG2): /* PL - just warn for now. */
            case SUSP_MAKE_SIG(ISO9660RRIPRE_SIG1, ISO9660RRIPRE_SIG2): /* RE - just warn for now. */
                Log4(("rtFsIsoDir_ParseRockRidgeData: Ignorning directory relocation entry '%c%c'!\n", pUnion->Hdr.bSig1, pUnion->Hdr.bSig2));
                break;

            default:
                Log4(("rtFsIsoDir_ParseRockRidgeData: Unknown SUSP entry: %#x %#x, %#x bytes, v%u\n",
                      pUnion->Hdr.bSig1, pUnion->Hdr.bSig2, pUnion->Hdr.cbEntry, pUnion->Hdr.bVersion));
                break;
        }
    }

    /*
     * Set the valid flag if we found anything of interest.
     */
    if (pParseInfo->cRockEntries > 1)
        pParseInfo->fValid = true;
}


/**
 * Initializes the rock info structure with info from the standard ISO-9660
 * directory record.
 *
 * @param   pRockInfo   The structure to initialize.
 * @param   pDirRec     The directory record to take basic data from.
 */
static void rtFsIsoDirShrd_InitRockInfo(PRTFSISOROCKINFO pRockInfo, PCISO9660DIRREC pDirRec)
{
    pRockInfo->fValid                           = false;
    pRockInfo->fSuspSeenSP                      = false;
    pRockInfo->fSeenLastNM                      = false;
    pRockInfo->fSeenLastSL                      = false;
    pRockInfo->fOverflowSL                      = false;
    pRockInfo->cRockEntries                     = 0;
    pRockInfo->cchName                          = 0;
    pRockInfo->cchLinkTarget                    = 0;
    pRockInfo->szName[0]                        = '\0';
    pRockInfo->szName[sizeof(pRockInfo->szName) - 1] = '\0';
    pRockInfo->szLinkTarget[0]                  = '\0';
    pRockInfo->szLinkTarget[sizeof(pRockInfo->szLinkTarget) - 1] = '\0';
    pRockInfo->Info.cbObject                    = ISO9660_GET_ENDIAN(&pDirRec->cbData);
    pRockInfo->Info.cbAllocated                 = pRockInfo->Info.cbObject;
    rtFsIso9660DateTime2TimeSpec(&pRockInfo->Info.AccessTime, &pDirRec->RecTime);
    pRockInfo->Info.ModificationTime            = pRockInfo->Info.AccessTime;
    pRockInfo->Info.ChangeTime                  = pRockInfo->Info.AccessTime;
    pRockInfo->Info.BirthTime                   = pRockInfo->Info.AccessTime;
    pRockInfo->Info.Attr.fMode                  = pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY
                                                ? RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY | 0555
                                                : RTFS_TYPE_FILE      | RTFS_DOS_ARCHIVED  | 0444;
    if (pDirRec->fFileFlags & ISO9660_FILE_FLAGS_HIDDEN)
        pRockInfo->Info.Attr.fMode             |= RTFS_DOS_HIDDEN;
    pRockInfo->Info.Attr.enmAdditional          = RTFSOBJATTRADD_UNIX;
    pRockInfo->Info.Attr.u.Unix.uid             = NIL_RTUID;
    pRockInfo->Info.Attr.u.Unix.gid             = NIL_RTGID;
    pRockInfo->Info.Attr.u.Unix.cHardlinks      = 1;
    pRockInfo->Info.Attr.u.Unix.INodeIdDevice   = 0;
    pRockInfo->Info.Attr.u.Unix.INodeId         = 0;
    pRockInfo->Info.Attr.u.Unix.fFlags          = 0;
    pRockInfo->Info.Attr.u.Unix.GenerationId    = 0;
    pRockInfo->Info.Attr.u.Unix.Device          = 0;
}


static void rtFsIsoDirShrd_ParseRockForDirRec(PRTFSISODIRSHRD pThis, PCISO9660DIRREC pDirRec, PRTFSISOROCKINFO pRockInfo)
{
    rtFsIsoDirShrd_InitRockInfo(pRockInfo, pDirRec); /* Always! */

    PRTFSISOVOL const pVol  = pThis->Core.pVol;
    uint8_t           cbSys = pDirRec->cbDirRec - RT_UOFFSETOF(ISO9660DIRREC, achFileId)
                            - pDirRec->bFileIdLength - !(pDirRec->bFileIdLength & 1);
    uint8_t const    *pbSys = (uint8_t const *)&pDirRec->achFileId[pDirRec->bFileIdLength + !(pDirRec->bFileIdLength & 1)];
    if (cbSys >= 4 + pVol->offSuspSkip)
    {
        pbSys += pVol->offSuspSkip;
        cbSys -= pVol->offSuspSkip;
        rtFsIsoDirShrd_ParseRockRidgeData(pVol, pRockInfo, pbSys, cbSys,
                                          false /*fIsFirstDirRec*/, false /*fContinuationRecord*/);
    }
}


static void rtFsIsoDirShrd_ParseRockForRoot(PRTFSISODIRSHRD pThis, PCISO9660DIRREC pDirRec)
{
    uint8_t const         cbSys = pDirRec->cbDirRec - RT_UOFFSETOF(ISO9660DIRREC, achFileId)
                                - pDirRec->bFileIdLength - !(pDirRec->bFileIdLength & 1);
    uint8_t const * const pbSys = (uint8_t const *)&pDirRec->achFileId[pDirRec->bFileIdLength + !(pDirRec->bFileIdLength & 1)];
    if (cbSys >= 4)
    {
        RTFSISOROCKINFO RockInfo;
        rtFsIsoDirShrd_InitRockInfo(&RockInfo, pDirRec);
        rtFsIsoDirShrd_ParseRockRidgeData(pThis->Core.pVol, &RockInfo, pbSys, cbSys,
                                          true /*fIsFirstDirRec*/, false /*fContinuationRecord*/);
        if (RockInfo.fValid)
        {
            pThis->Core.fHaveRockInfo    = true;
            pThis->Core.BirthTime        = RockInfo.Info.BirthTime;
            pThis->Core.ChangeTime       = RockInfo.Info.ChangeTime;
            pThis->Core.AccessTime       = RockInfo.Info.AccessTime;
            pThis->Core.ModificationTime = RockInfo.Info.ModificationTime;
            if (RTFS_IS_DIRECTORY(RockInfo.Info.Attr.fMode))
                pThis->Core.fAttrib      = RockInfo.Info.Attr.fMode;
        }
    }
}


/**
 * Compares rock ridge information if present in the directory entry.
 *
 * @param   pThis               The shared directory structure.
 * @param   pbSys               The system area of the directory record.
 * @param   cbSys               The number of bytes present in the sys area.
 * @param   pNameCmp            The name comparsion data.
 * @param   fContinuationRecord Set if we're processing a continuation record in
 *                              living in the abRockBuf.
 */
static int rtFsIsoDirShrd_CompareRockRidgeName(PRTFSISODIRSHRD pThis, uint8_t const *pbSys, size_t cbSys,
                                               PRTFSISOROCKNAMECOMP pNameCmp, bool fContinuationRecord)
{
    PRTFSISOVOL const pVol = pThis->Core.pVol;

    /*
     * Do skipping if specified.
     */
    if (pVol->offSuspSkip)
    {
        if (cbSys <= pVol->offSuspSkip)
            return fContinuationRecord ? VERR_MORE_DATA : VERR_MISMATCH;
        pbSys += pVol->offSuspSkip;
        cbSys -= pVol->offSuspSkip;
    }

    while (cbSys >= 4)
    {
        /*
         * Check header length and advance the sys variables.
         */
        PCISO9660SUSPUNION pUnion = (PCISO9660SUSPUNION)pbSys;
        if (   pUnion->Hdr.cbEntry > cbSys
            && pUnion->Hdr.cbEntry < sizeof(pUnion->Hdr))
        {
            Log4(("rtFsIsoDirShrd_CompareRockRidgeName: cbEntry=%#x cbSys=%#x (%#x %#x)\n",
                  pUnion->Hdr.cbEntry, cbSys, pUnion->Hdr.bSig1, pUnion->Hdr.bSig2));
            break;
        }
        pbSys += pUnion->Hdr.cbEntry;
        cbSys -= pUnion->Hdr.cbEntry;

        /*
         * Process the fields we need, nothing else.
         */
        uint16_t const uSig = SUSP_MAKE_SIG(pUnion->Hdr.bSig1, pUnion->Hdr.bSig2);


        /*
         * CE - continuation entry
         */
        if (uSig == SUSP_MAKE_SIG(ISO9660SUSPCE_SIG1, ISO9660SUSPCE_SIG2))
        {
            if (RT_BE2H_U32(pUnion->CE.offBlock.be) != RT_LE2H_U32(pUnion->CE.offBlock.le))
                Log4(("rtFsIsoDirShrd_CompareRockRidgeName: Invalid CE offBlock field: be=%#x vs le=%#x\n",
                      RT_BE2H_U32(pUnion->CE.offBlock.be), RT_LE2H_U32(pUnion->CE.offBlock.le)));
            else if (RT_BE2H_U32(pUnion->CE.cbData.be) != RT_LE2H_U32(pUnion->CE.cbData.le))
                Log4(("rtFsIsoDirShrd_CompareRockRidgeName: Invalid CE cbData field: be=%#x vs le=%#x\n",
                      RT_BE2H_U32(pUnion->CE.cbData.be), RT_LE2H_U32(pUnion->CE.cbData.le)));
            else if (RT_BE2H_U32(pUnion->CE.offData.be) != RT_LE2H_U32(pUnion->CE.offData.le))
                Log4(("rtFsIsoDirShrd_CompareRockRidgeName: Invalid CE offData field: be=%#x vs le=%#x\n",
                      RT_BE2H_U32(pUnion->CE.offData.be), RT_LE2H_U32(pUnion->CE.offData.le)));
            else if (!fContinuationRecord)
            {
                uint64_t offData = ISO9660_GET_ENDIAN(&pUnion->CE.offBlock) * (uint64_t)ISO9660_SECTOR_SIZE;
                offData += ISO9660_GET_ENDIAN(&pUnion->CE.offData);
                uint32_t cbData  = ISO9660_GET_ENDIAN(&pUnion->CE.cbData);
                if (cbData <= sizeof(pVol->abRockBuf) - (uint32_t)(offData & ISO9660_SECTOR_OFFSET_MASK))
                {
                    RTCritSectEnter(&pVol->RockBufLock);

                    AssertCompile(sizeof(pVol->abRockBuf) == ISO9660_SECTOR_SIZE);
                    uint64_t offDataBlock = offData & ~(uint64_t)ISO9660_SECTOR_OFFSET_MASK;
                    int rc;
                    if (pVol->offRockBuf == offDataBlock)
                        rc = rtFsIsoDirShrd_CompareRockRidgeName(pThis, &pVol->abRockBuf[offData & ISO9660_SECTOR_OFFSET_MASK],
                                                                 cbData, pNameCmp, true /*fContinuationRecord*/);
                    else
                    {
                        rc = RTVfsFileReadAt(pVol->hVfsBacking, offDataBlock, pVol->abRockBuf, sizeof(pVol->abRockBuf), NULL);
                        if (RT_SUCCESS(rc))
                            rc = rtFsIsoDirShrd_CompareRockRidgeName(pThis, &pVol->abRockBuf[offData & ISO9660_SECTOR_OFFSET_MASK],
                                                                     cbData, pNameCmp, true /*fContinuationRecord*/);
                        else
                            Log4(("rtFsIsoDirShrd_CompareRockRidgeName: Error reading continuation record at %#RX64: %Rrc\n",
                                  offDataBlock, rc));
                    }

                    RTCritSectLeave(&pVol->RockBufLock);
                    if (rc != VERR_MORE_DATA)
                        return rc;
                }
                else
                    Log4(("rtFsIsoDirShrd_CompareRockRidgeName: continuation record isn't within a sector! offData=%#RX64 cbData=%#RX32\n",
                          cbData, offData));
            }
            else
                Log4(("rtFsIsoDirShrd_CompareRockRidgeName: nested continuation record!\n"));
        }
        /*
         * NM - Name entry.
         *
         * The character set is supposed to be limited to the portable filename
         * character set defined in section 2.2.2.60 of POSIX.1: A-Za-z0-9._-
         * If there are any other characters used, we consider them as UTF-8
         * for reasons of simplicitiy, however we do not make any effort dealing
         * with codepoint encodings across NM records for now because it is
         * probably a complete waste of time.
         */
        else if (uSig == SUSP_MAKE_SIG(ISO9660RRIPNM_SIG1, ISO9660RRIPNM_SIG2))
        {
            if (   pUnion->NM.Hdr.bVersion != ISO9660RRIPNM_VER
                || pUnion->NM.Hdr.cbEntry < RT_UOFFSETOF(ISO9660RRIPNM, achName)
                || (pUnion->NM.fFlags & ISO9660RRIP_NM_F_RESERVED_MASK) )
                Log4(("rtFsIsoDirShrd_CompareRockRidgeName: Malformed 'NM' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x %.*Rhxs\n",
                      pUnion->NM.Hdr.cbEntry, RT_UOFFSETOF(ISO9660RRIPNM, achName),
                      pUnion->NM.Hdr.bVersion, ISO9660RRIPNM_VER, pUnion->NM.fFlags,
                      pUnion->NM.Hdr.cbEntry - RT_MIN(pUnion->NM.Hdr.cbEntry, RT_UOFFSETOF(ISO9660RRIPNM, achName)),
                      &pUnion->NM.achName[0] ));
            else
            {
                uint8_t const cchName = pUnion->NM.Hdr.cbEntry - (uint8_t)RT_UOFFSETOF(ISO9660RRIPNM, achName);
                if (!(pUnion->NM.fFlags & (ISO9660RRIP_NM_F_CURRENT | ISO9660RRIP_NM_F_PARENT)))
                { /* likely */ }
                else
                {
                    if (cchName == 0)
                        Log4(("rtFsIsoDirShrd_CompareRockRidgeName: Ignoring 'NM' entry for '.' and '..'\n"));
                    else
                        Log4(("rtFsIsoDirShrd_CompareRockRidgeName: Ignoring malformed 'NM' using '.' or '..': fFlags=%#x cchName=%#x %.*Rhxs\n",
                              pUnion->NM.fFlags, cchName, cchName, pUnion->NM.achName));
                    pNameCmp->offMatched = ~(size_t)0 / 2;
                    return VERR_MISMATCH;
                }
                Log4(("rtFsIsoDirShrd_CompareRockRidgeName: 'NM': fFlags=%#x cchName=%#x '%.*s' (%.*Rhxs); offMatched=%#zx cchEntry=%#zx\n",
                      pUnion->NM.fFlags, cchName, cchName, pUnion->NM.achName, cchName, pUnion->NM.achName, pNameCmp->offMatched, pNameCmp->cchEntry));
                AssertReturn(pNameCmp->offMatched < pNameCmp->cchEntry, VERR_MISMATCH);

                if (RTStrNICmp(&pNameCmp->pszEntry[pNameCmp->offMatched], pUnion->NM.achName, cchName) == 0)
                {
                    /** @todo Incorrectly ASSUMES all upper and lower codepoints have the same
                     *        encoding length.  However, since this shouldn't be UTF-8, but plain
                     *        limited ASCII that's not really all that important. */
                    pNameCmp->offMatched += cchName;
                    if (!(pUnion->NM.fFlags & ISO9660RRIP_NM_F_CONTINUE))
                    {
                        if (pNameCmp->offMatched >= pNameCmp->cchEntry)
                        {
                            Log4(("rtFsIsoDirShrd_CompareRockRidgeName: 'NM': returning VINF_SUCCESS\n"));
                            return VINF_SUCCESS;
                        }
                        Log4(("rtFsIsoDirShrd_CompareRockRidgeName: 'NM': returning VERR_MISMATCH - %zu unmatched bytes\n",
                              pNameCmp->cchEntry - pNameCmp->offMatched));
                        return VERR_MISMATCH;
                    }
                    if (pNameCmp->offMatched >= pNameCmp->cchEntry)
                    {
                        Log4(("rtFsIsoDirShrd_CompareRockRidgeName: 'NM': returning VERR_MISMATCH - match full name but ISO9660RRIP_NM_F_CONTINUE is set!\n"));
                        return VERR_MISMATCH;
                    }
                }
                else
                {
                    Log4(("rtFsIsoDirShrd_CompareRockRidgeName: 'NM': returning VERR_MISMATCH - mismatch\n"));
                    pNameCmp->offMatched = ~(size_t)0 / 2;
                    return VERR_MISMATCH;
                }
            }
        }
    }
    return fContinuationRecord ? VERR_MORE_DATA : VERR_MISMATCH;
}


/**
 * Worker for rtFsIsoDir_FindEntry9660 that compares a name with the rock ridge
 * info in the directory record, if present.
 *
 * @returns true if equal, false if not.
 * @param   pThis               The directory.
 * @param   pDirRec             The directory record.
 * @param   pszEntry            The string to compare with.
 * @param   cbEntry             The length of @a pszEntry including terminator.
 */
static bool rtFsIsoDir_IsEntryEqualRock(PRTFSISODIRSHRD pThis, PCISO9660DIRREC pDirRec, const char *pszEntry, size_t cbEntry)
{
    /*
     * Is there room for any rock ridge data?
     */
    uint8_t const         cbSys = pDirRec->cbDirRec - RT_UOFFSETOF(ISO9660DIRREC, achFileId)
                                - pDirRec->bFileIdLength - !(pDirRec->bFileIdLength & 1);
    uint8_t const * const pbSys = (uint8_t const *)&pDirRec->achFileId[pDirRec->bFileIdLength + !(pDirRec->bFileIdLength & 1)];
    if (cbSys >= 4)
    {
        RTFSISOROCKNAMECOMP NameCmp;
        NameCmp.pszEntry   = pszEntry;
        NameCmp.cchEntry   = cbEntry - 1;
        NameCmp.offMatched = 0;
        int rc = rtFsIsoDirShrd_CompareRockRidgeName(pThis, pbSys, cbSys, &NameCmp, false /*fContinuationRecord*/);
        if (rc == VINF_SUCCESS)
            return true;
    }
    return false;
}


/**
 * Worker for rtFsIsoDir_FindEntry9660 that compares a UTF-16BE name with a
 * directory record.
 *
 * @returns true if equal, false if not.
 * @param   pDirRec             The directory record.
 * @param   pwszEntry           The UTF-16BE string to compare with.
 * @param   cbEntry             The compare string length in bytes (sans zero
 *                              terminator).
 * @param   cwcEntry            The compare string length in RTUTF16 units.
 * @param   puVersion           Where to return any file version number.
 */
DECL_FORCE_INLINE(bool) rtFsIsoDir_IsEntryEqualUtf16Big(PCISO9660DIRREC pDirRec, PCRTUTF16 pwszEntry, size_t cbEntry,
                                                        size_t cwcEntry, uint32_t *puVersion)
{
    /* ASSUME directories cannot have any version tags. */
    if (pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY)
    {
        if (RT_LIKELY(pDirRec->bFileIdLength != cbEntry))
            return false;
        if (RT_LIKELY(RTUtf16BigNICmp((PCRTUTF16)pDirRec->achFileId, pwszEntry, cwcEntry) != 0))
            return false;
    }
    else
    {
        size_t cbNameDelta = (size_t)pDirRec->bFileIdLength - cbEntry;
        if (RT_LIKELY(cbNameDelta > (size_t)12 /* ;12345 */))
            return false;
        if (cbNameDelta == 0)
        {
            if (RT_LIKELY(RTUtf16BigNICmp((PCRTUTF16)pDirRec->achFileId, pwszEntry, cwcEntry) != 0))
                return false;
            *puVersion = 1;
        }
        else
        {
            if (RT_LIKELY(RT_MAKE_U16(pDirRec->achFileId[cbEntry + 1], pDirRec->achFileId[cbEntry]) != ';'))
                return false;
            if (RT_LIKELY(RTUtf16BigNICmp((PCRTUTF16)pDirRec->achFileId, pwszEntry, cwcEntry) != 0))
                return false;
            uint32_t uVersion;
            size_t  cwcVersion = rtFsIso9660GetVersionLengthUtf16Big((PCRTUTF16)pDirRec->achFileId,
                                                                     pDirRec->bFileIdLength, &uVersion);
            if (RT_LIKELY(cwcVersion * sizeof(RTUTF16) == cbNameDelta))
                *puVersion = uVersion;
            else
                return false;
        }
    }

    /* (No need to check for dot and dot-dot here, because cbEntry must be a
       multiple of two.) */
    Assert(!(cbEntry & 1));
    return true;
}


/**
 * Worker for rtFsIsoDir_FindEntry9660 that compares an ASCII name with a
 * directory record.
 *
 * @returns true if equal, false if not.
 * @param   pDirRec             The directory record.
 * @param   pszEntry            The uppercased ASCII string to compare with.
 * @param   cchEntry            The length of the compare string.
 * @param   puVersion           Where to return any file version number.
 *
 * @note    We're using RTStrNICmpAscii here because of non-conforming ISOs with
 *          entirely lowercase name or mixed cased names.
 */
DECL_FORCE_INLINE(bool) rtFsIsoDir_IsEntryEqualAscii(PCISO9660DIRREC pDirRec, const char *pszEntry, size_t cchEntry,
                                                     uint32_t *puVersion)
{
    /* ASSUME directories cannot have any version tags. */
    if (pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY)
    {
        if (RT_LIKELY(pDirRec->bFileIdLength != cchEntry))
            return false;
        if (RT_LIKELY(RTStrNICmpAscii(pDirRec->achFileId, pszEntry, cchEntry) != 0))
            return false;
    }
    else
    {
        size_t cchNameDelta = (size_t)pDirRec->bFileIdLength - cchEntry;
        if (RT_LIKELY(cchNameDelta > (size_t)6 /* ;12345 */))
            return false;
        if (cchNameDelta == 0)
        {
            if (RT_LIKELY(RTStrNICmpAscii(pDirRec->achFileId, pszEntry, cchEntry) != 0))
                return false;
            *puVersion = 1;
        }
        else
        {
            if (RT_LIKELY(pDirRec->achFileId[cchEntry] != ';'))
                return false;
            if (RT_LIKELY(RTStrNICmpAscii(pDirRec->achFileId, pszEntry, cchEntry) != 0))
                return false;
            uint32_t uVersion;
            size_t  cchVersion = rtFsIso9660GetVersionLengthAscii(pDirRec->achFileId, pDirRec->bFileIdLength, &uVersion);
            if (RT_LIKELY(cchVersion == cchNameDelta))
                *puVersion = uVersion;
            else
                return false;
        }
    }

    /* Don't match the 'dot' and 'dot-dot' directory records. */
    if (RT_LIKELY(   pDirRec->bFileIdLength != 1
                  || (uint8_t)pDirRec->achFileId[0] > (uint8_t)0x01))
        return true;
    return false;
}


/**
 * Locates a directory entry in a directory.
 *
 * @returns IPRT status code.
 * @retval  VERR_FILE_NOT_FOUND if not found.
 * @param   pThis           The directory to search.
 * @param   pszEntry        The entry to look for.
 * @param   poffDirRec      Where to return the offset of the directory record
 *                          on the disk.
 * @param   ppDirRec        Where to return the pointer to the directory record
 *                          (the whole directory is buffered).
 * @param   pcDirRecs       Where to return the number of directory records
 *                          related to this entry.
 * @param   pfMode          Where to return the file type, rock ridge adjusted.
 * @param   puVersion       Where to return the file version number.
 * @param   pRockInfo       Where to return rock ridge info.  This is NULL if
 *                          the volume didn't advertise any rock ridge info.
 */
static int rtFsIsoDir_FindEntry9660(PRTFSISODIRSHRD pThis, const char *pszEntry, uint64_t *poffDirRec, PCISO9660DIRREC *ppDirRec,
                                    uint32_t *pcDirRecs, PRTFMODE pfMode, uint32_t *puVersion, PRTFSISOROCKINFO pRockInfo)
{
    Assert(pThis->Core.pVol->enmType != RTFSISOVOLTYPE_UDF);

    /* Set return values. */
    *poffDirRec = UINT64_MAX;
    *ppDirRec   = NULL;
    *pcDirRecs  = 1;
    *pfMode     = UINT32_MAX;
    *puVersion  = 0;
    if (pRockInfo)
        pRockInfo->fValid = false;

    /*
     * If we're in UTF-16BE mode, convert the input name to UTF-16BE.  Otherwise try
     * uppercase it into a ISO 9660 compliant name.
     */
    int         rc;
    bool const  fIsUtf16 = pThis->Core.pVol->fIsUtf16;
    size_t      cwcEntry = 0;
    size_t      cbEntry  = 0;
    size_t      cchUpper = ~(size_t)0;
    union
    {
        RTUTF16  wszEntry[260 + 1];
        struct
        {
            char  szUpper[255 + 1];
            char  szRock[260 + 1];
        } s;
    } uBuf;
    if (fIsUtf16)
    {
        PRTUTF16 pwszEntry = uBuf.wszEntry;
        rc = RTStrToUtf16BigEx(pszEntry, RTSTR_MAX, &pwszEntry, RT_ELEMENTS(uBuf.wszEntry), &cwcEntry);
        if (RT_FAILURE(rc))
            return rc == VERR_BUFFER_OVERFLOW ? VERR_FILENAME_TOO_LONG : rc;
        cbEntry = cwcEntry * 2;
    }
    else
    {
        rc = RTStrCopy(uBuf.s.szUpper, sizeof(uBuf.s.szUpper), pszEntry);
        if (RT_FAILURE(rc))
            return rc == VERR_BUFFER_OVERFLOW ? VERR_FILENAME_TOO_LONG : rc;
        RTStrToUpper(uBuf.s.szUpper);
        cchUpper = strlen(uBuf.s.szUpper);
        cbEntry  = strlen(pszEntry) + 1;
    }

    /*
     * Scan the directory buffer by buffer.
     */
    uint32_t        offEntryInDir   = 0;
    uint32_t const  cbDir           = pThis->Core.cbObject;
    while (offEntryInDir + RT_UOFFSETOF(ISO9660DIRREC, achFileId) <= cbDir)
    {
        PCISO9660DIRREC pDirRec = (PCISO9660DIRREC)&pThis->pbDir[offEntryInDir];

        /* If null length, skip to the next sector. */
        if (pDirRec->cbDirRec == 0)
            offEntryInDir = (offEntryInDir + pThis->Core.pVol->cbSector) & ~(pThis->Core.pVol->cbSector - 1U);
        else
        {
            /*
             * Try match the filename.
             */
            /** @todo not sure if it's a great idea to match both name spaces...   */
            if (RT_LIKELY(  fIsUtf16
                          ?    !rtFsIsoDir_IsEntryEqualUtf16Big(pDirRec, uBuf.wszEntry, cbEntry, cwcEntry, puVersion)
                            && (   !pRockInfo
                                || !rtFsIsoDir_IsEntryEqualRock(pThis, pDirRec, pszEntry, cbEntry))
                          :    (   !pRockInfo
                                || !rtFsIsoDir_IsEntryEqualRock(pThis, pDirRec, pszEntry, cbEntry))
                            && !rtFsIsoDir_IsEntryEqualAscii(pDirRec, uBuf.s.szUpper, cchUpper, puVersion) ))
            {
                /* Advance */
                offEntryInDir += pDirRec->cbDirRec;
                continue;
            }

            /*
             * Get info for the entry.
             */
            if (!pRockInfo)
                *pfMode  = pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY
                         ? 0755 | RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY
                         : 0644 | RTFS_TYPE_FILE;
            else
            {
                rtFsIsoDirShrd_ParseRockForDirRec(pThis, pDirRec, pRockInfo);
                *pfMode = pRockInfo->Info.Attr.fMode;
            }
            *poffDirRec = pThis->Core.FirstExtent.off + offEntryInDir;
            *ppDirRec   = pDirRec;

            /*
             * Deal with the unlikely scenario of multi extent records.
             */
            if (!(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT))
                *pcDirRecs = 1;
            else
            {
                offEntryInDir += pDirRec->cbDirRec;

                uint32_t cDirRecs = 1;
                while (offEntryInDir + RT_UOFFSETOF(ISO9660DIRREC, achFileId) <= cbDir)
                {
                    PCISO9660DIRREC pDirRec2 = (PCISO9660DIRREC)&pThis->pbDir[offEntryInDir];
                    if (pDirRec2->cbDirRec != 0)
                    {
                        Assert(rtFsIsoDir_Is9660DirRecNextExtent(pDirRec, pDirRec2));
                        cDirRecs++;
                        if (!(pDirRec2->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT))
                            break;
                        offEntryInDir += pDirRec2->cbDirRec;
                    }
                    else
                        offEntryInDir = (offEntryInDir + pThis->Core.pVol->cbSector) & ~(pThis->Core.pVol->cbSector - 1U);
                }

                *pcDirRecs = cDirRecs;
            }
            return VINF_SUCCESS;
        }
    }

    return VERR_FILE_NOT_FOUND;
}


/**
 * Locates a directory entry in a directory.
 *
 * @returns IPRT status code.
 * @retval  VERR_FILE_NOT_FOUND if not found.
 * @param   pThis           The directory to search.
 * @param   pszEntry        The entry to look for.
 * @param   ppFid           Where to return the pointer to the file ID entry.
 *                          (Points to the directory content.)
 */
static int rtFsIsoDir_FindEntryUdf(PRTFSISODIRSHRD pThis, const char *pszEntry, PCUDFFILEIDDESC *ppFid)
{
    Assert(pThis->Core.pVol->enmType == RTFSISOVOLTYPE_UDF);
    *ppFid = NULL;

    /*
     * Recode the entry name as 8-bit (if possible) and 16-bit strings.
     * This also disposes of entries that definitely are too long.
     */
    size_t   cb8Bit;
    bool     fSimple;
    size_t   cb16Bit;
    size_t   cwc16Bit;
    uint8_t  ab8Bit[255];
    RTUTF16  wsz16Bit[255];

    /* 16-bit */
    PRTUTF16  pwsz16Bit = wsz16Bit;
    int rc = RTStrToUtf16BigEx(pszEntry, RTSTR_MAX, &pwsz16Bit, RT_ELEMENTS(wsz16Bit), &cwc16Bit);
    if (RT_SUCCESS(rc))
        cb16Bit = 1 + cwc16Bit * sizeof(RTUTF16);
    else
        return rc == VERR_BUFFER_OVERFLOW ? VERR_FILENAME_TOO_LONG : rc;

    /* 8-bit (can't possibly overflow) */
    fSimple = true;
    cb8Bit = 0;
    const char *pszSrc = pszEntry;
    for (;;)
    {
        RTUNICP uc;
        int rc2 = RTStrGetCpEx(&pszSrc, &uc);
        AssertRCReturn(rc2, rc2);
        if (uc <= 0x7f)
        {
            if (uc)
                ab8Bit[cb8Bit++] = (uint8_t)uc;
            else
                break;
        }
        else if (uc <= 0xff)
        {
            ab8Bit[cb8Bit++] = (uint8_t)uc;
            fSimple = false;
        }
        else
        {
            cb8Bit = UINT32_MAX / 2;
            break;
        }
    }
    Assert(cb8Bit <= sizeof(ab8Bit) || cb8Bit == UINT32_MAX / 2);
    cb8Bit++;

    /*
     * Scan the directory content.
     */
    uint32_t        offDesc = 0;
    uint32_t const  cbDir   = pThis->Core.cbObject;
    while (offDesc + RT_UOFFSETOF(UDFFILEIDDESC, abImplementationUse) <= cbDir)
    {
        PCUDFFILEIDDESC pFid  = (PCUDFFILEIDDESC)&pThis->pbDir[offDesc];
        uint32_t const  cbFid = UDFFILEIDDESC_GET_SIZE(pFid);
        if (   offDesc + cbFid <= cbDir
            && pFid->Tag.idTag == UDF_TAG_ID_FILE_ID_DESC)
        { /* likely */ }
        else
            break;

        uint8_t const *pbName = UDFFILEIDDESC_2_NAME(pFid);
        if (*pbName == 16)
        {
            if (cb16Bit == pFid->cbName)
            {
                if (RTUtf16BigNICmp((PCRTUTF16)(&pbName[1]), wsz16Bit, cwc16Bit) == 0)
                {
                    *ppFid = pFid;
                    return VINF_SUCCESS;
                }
            }
        }
        else if (*pbName == 8)
        {
            if (   cb8Bit != UINT16_MAX
                && cb8Bit == pFid->cbName)
            {
                if (fSimple)
                {
                    if (RTStrNICmp((const char *)&pbName[1], (const char *)ab8Bit, cb8Bit - 1) == 0)
                    {
                        *ppFid = pFid;
                        return VINF_SUCCESS;
                    }
                }
                else
                {
                    size_t cch = cb8Bit - 1;
                    size_t off;
                    for (off = 0; off < cch; off++)
                    {
                        RTUNICP uc1 = ab8Bit[off];
                        RTUNICP uc2 = pbName[off + 1];
                        if (   uc1 == uc2
                            || RTUniCpToLower(uc1) == RTUniCpToLower(uc2)
                            || RTUniCpToUpper(uc1) == RTUniCpToUpper(uc2))
                        { /* matches */ }
                        else
                            break;
                    }
                    if (off == cch)
                    {
                        *ppFid = pFid;
                        return VINF_SUCCESS;
                    }
                }
            }
        }

        /* advance */
        offDesc += cbFid;
    }

    return VERR_FILE_NOT_FOUND;
}


/**
 * Releases a reference to a shared directory structure.
 *
 * @param   pShared             The shared directory structure.
 */
static void rtFsIsoDirShrd_Release(PRTFSISODIRSHRD pShared)
{
    uint32_t cRefs = ASMAtomicDecU32(&pShared->Core.cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (cRefs == 0)
    {
        LogFlow(("rtFsIsoDirShrd_Release: Destroying shared structure %p\n", pShared));
        Assert(pShared->Core.cRefs == 0);
        if (pShared->pbDir)
        {
            RTMemFree(pShared->pbDir);
            pShared->pbDir = NULL;
        }
        rtFsIsoCore_Destroy(&pShared->Core);
        RTMemFree(pShared);
    }
}


/**
 * Retains a reference to a shared directory structure.
 *
 * @param   pShared             The shared directory structure.
 */
static void rtFsIsoDirShrd_Retain(PRTFSISODIRSHRD pShared)
{
    uint32_t cRefs = ASMAtomicIncU32(&pShared->Core.cRefs);
    Assert(cRefs > 1); NOREF(cRefs);
}



/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtFsIsoDir_Close(void *pvThis)
{
    PRTFSISODIROBJ pThis = (PRTFSISODIROBJ)pvThis;
    LogFlow(("rtFsIsoDir_Close(%p/%p)\n", pThis, pThis->pShared));

    PRTFSISODIRSHRD pShared = pThis->pShared;
    pThis->pShared = NULL;
    if (pShared)
        rtFsIsoDirShrd_Release(pShared);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsIsoDir_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSISODIROBJ pThis = (PRTFSISODIROBJ)pvThis;
    return rtFsIsoCore_QueryInfo(&pThis->pShared->Core, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpen}
 */
static DECLCALLBACK(int) rtFsIsoDir_Open(void *pvThis, const char *pszEntry, uint64_t fOpen,
                                         uint32_t fFlags, PRTVFSOBJ phVfsObj)
{
    PRTFSISODIROBJ  pThis   = (PRTFSISODIROBJ)pvThis;
    PRTFSISODIRSHRD pShared = pThis->pShared;
    int rc;

    /*
     * We cannot create or replace anything, just open stuff.
     */
    if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN
        || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE)
    { /* likely */ }
    else
        return VERR_WRITE_PROTECT;

    /*
     * Special cases '.' and '..'
     */
    if (pszEntry[0] == '.')
    {
        PRTFSISODIRSHRD pSharedToOpen;
        if (pszEntry[1] == '\0')
            pSharedToOpen = pShared;
        else if (pszEntry[1] == '.' && pszEntry[2] == '\0')
        {
            pSharedToOpen = pShared->Core.pParentDir;
            if (!pSharedToOpen)
                pSharedToOpen = pShared;
        }
        else
            pSharedToOpen = NULL;
        if (pSharedToOpen)
        {
            if (fFlags & RTVFSOBJ_F_OPEN_DIRECTORY)
            {
                rtFsIsoDirShrd_Retain(pSharedToOpen);
                RTVFSDIR hVfsDir;
                rc = rtFsIsoDir_NewWithShared(pShared->Core.pVol, pSharedToOpen, &hVfsDir);
                if (RT_SUCCESS(rc))
                {
                    *phVfsObj = RTVfsObjFromDir(hVfsDir);
                    RTVfsDirRelease(hVfsDir);
                    AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                }
            }
            else
                rc = VERR_IS_A_DIRECTORY;
            return rc;
        }
    }

    /*
     * Try open whatever it is.
     */
    if (pShared->Core.pVol->enmType != RTFSISOVOLTYPE_UDF)
    {

        /*
         * ISO 9660
         */
        PCISO9660DIRREC     pDirRec;
        uint64_t            offDirRec;
        uint32_t            cDirRecs;
        RTFMODE             fMode;
        uint32_t            uVersion;
        PRTFSISOROCKINFO    pRockInfo = NULL;
        if (pShared->Core.pVol->fHaveRock)
            pRockInfo = (PRTFSISOROCKINFO)alloca(sizeof(*pRockInfo));
        rc = rtFsIsoDir_FindEntry9660(pShared, pszEntry, &offDirRec, &pDirRec, &cDirRecs, &fMode, &uVersion, pRockInfo);
        Log2(("rtFsIsoDir_Open: FindEntry9660(,%s,) -> %Rrc\n", pszEntry, rc));
        if (RT_SUCCESS(rc))
        {
            switch (fMode & RTFS_TYPE_MASK)
            {
                case RTFS_TYPE_FILE:
                    if (fFlags & RTVFSOBJ_F_OPEN_FILE)
                    {
                        RTVFSFILE hVfsFile;
                        rc = rtFsIsoFile_New9660(pShared->Core.pVol, pShared, pDirRec, cDirRecs, offDirRec, fOpen,
                                                 uVersion, pRockInfo && pRockInfo->fValid ? pRockInfo : NULL, &hVfsFile);
                        if (RT_SUCCESS(rc))
                        {
                            *phVfsObj = RTVfsObjFromFile(hVfsFile);
                            RTVfsFileRelease(hVfsFile);
                            AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                        }
                    }
                    else
                        rc = VERR_IS_A_FILE;
                    break;

                case RTFS_TYPE_DIRECTORY:
                    if (fFlags & RTVFSOBJ_F_OPEN_DIRECTORY)
                    {
                        RTVFSDIR hVfsDir;
                        rc = rtFsIsoDir_New9660(pShared->Core.pVol, pShared, pDirRec, cDirRecs, offDirRec,
                                                pRockInfo && pRockInfo->fValid ? pRockInfo : NULL, &hVfsDir);
                        if (RT_SUCCESS(rc))
                        {
                            *phVfsObj = RTVfsObjFromDir(hVfsDir);
                            RTVfsDirRelease(hVfsDir);
                            AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                        }
                    }
                    else
                        rc = VERR_IS_A_DIRECTORY;
                    break;

                case RTFS_TYPE_SYMLINK:
                case RTFS_TYPE_DEV_BLOCK:
                case RTFS_TYPE_DEV_CHAR:
                case RTFS_TYPE_FIFO:
                case RTFS_TYPE_SOCKET:
                case RTFS_TYPE_WHITEOUT:
                    rc = VERR_NOT_IMPLEMENTED;
                    break;

                default:
                    rc = VERR_PATH_NOT_FOUND;
                    break;
            }
        }
    }
    else
    {
        /*
         * UDF
         */
        PCUDFFILEIDDESC pFid;
        rc = rtFsIsoDir_FindEntryUdf(pShared, pszEntry, &pFid);
        Log2(("rtFsIsoDir_Open: FindEntryUdf(,%s,) -> %Rrc\n", pszEntry, rc));
        if (RT_SUCCESS(rc))
        {
            if (!(pFid->fFlags & UDF_FILE_FLAGS_DELETED))
            {
                if (!(pFid->fFlags & UDF_FILE_FLAGS_DIRECTORY))
                {
                    if (fFlags & RTVFSOBJ_F_OPEN_FILE)
                    {
                        RTVFSFILE hVfsFile;
                        rc = rtFsIsoFile_NewUdf(pShared->Core.pVol, pShared, pFid, fOpen, &hVfsFile);
                        if (RT_SUCCESS(rc))
                        {
                            *phVfsObj = RTVfsObjFromFile(hVfsFile);
                            RTVfsFileRelease(hVfsFile);
                            AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                        }
                    }
                    else
                        rc = VERR_IS_A_FILE;
                }
                else
                {
                    if (fFlags & RTVFSOBJ_F_OPEN_DIRECTORY)
                    {
                        RTVFSDIR hVfsDir;
                        rc = rtFsIsoDir_NewUdf(pShared->Core.pVol, pShared, pFid, &hVfsDir);
                        if (RT_SUCCESS(rc))
                        {
                            *phVfsObj = RTVfsObjFromDir(hVfsDir);
                            RTVfsDirRelease(hVfsDir);
                            AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                        }
                    }
                    else
                        rc = VERR_IS_A_DIRECTORY;
                }
            }
            /* We treat UDF_FILE_FLAGS_DELETED like RTFS_TYPE_WHITEOUT for now. */
            else
                rc = VERR_PATH_NOT_FOUND;
        }
    }
    return rc;

}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateDir}
 */
static DECLCALLBACK(int) rtFsIsoDir_CreateDir(void *pvThis, const char *pszSubDir, RTFMODE fMode, PRTVFSDIR phVfsDir)
{
    RT_NOREF(pvThis, pszSubDir, fMode, phVfsDir);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpenSymlink}
 */
static DECLCALLBACK(int) rtFsIsoDir_OpenSymlink(void *pvThis, const char *pszSymlink, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, phVfsSymlink);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateSymlink}
 */
static DECLCALLBACK(int) rtFsIsoDir_CreateSymlink(void *pvThis, const char *pszSymlink, const char *pszTarget,
                                                  RTSYMLINKTYPE enmType, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, pszTarget, enmType, phVfsSymlink);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnUnlinkEntry}
 */
static DECLCALLBACK(int) rtFsIsoDir_UnlinkEntry(void *pvThis, const char *pszEntry, RTFMODE fType)
{
    RT_NOREF(pvThis, pszEntry, fType);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRenameEntry}
 */
static DECLCALLBACK(int) rtFsIsoDir_RenameEntry(void *pvThis, const char *pszEntry, RTFMODE fType, const char *pszNewName)
{
    RT_NOREF(pvThis, pszEntry, fType, pszNewName);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRewindDir}
 */
static DECLCALLBACK(int) rtFsIsoDir_RewindDir(void *pvThis)
{
    PRTFSISODIROBJ pThis = (PRTFSISODIROBJ)pvThis;
    pThis->offDir = 0;
    return VINF_SUCCESS;
}


/**
 * The ISO 9660 worker for rtFsIsoDir_ReadDir
 */
static int rtFsIsoDir_ReadDir9660(PRTFSISODIROBJ pThis, PRTFSISODIRSHRD pShared, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                                  RTFSOBJATTRADD enmAddAttr)
{
    PRTFSISOROCKINFO    pRockInfo = NULL;
    if (pShared->Core.pVol->fHaveRock)
        pRockInfo = (PRTFSISOROCKINFO)alloca(sizeof(*pRockInfo));

    while (pThis->offDir + RT_UOFFSETOF(ISO9660DIRREC, achFileId) <= pShared->cbDir)
    {
        PCISO9660DIRREC pDirRec = (PCISO9660DIRREC)&pShared->pbDir[pThis->offDir];

        /* If null length, skip to the next sector. */
        if (pDirRec->cbDirRec == 0)
            pThis->offDir = (pThis->offDir + pShared->Core.pVol->cbSector) & ~(pShared->Core.pVol->cbSector - 1U);
        else
        {
            /*
             * Do names first as they may cause overflows.
             */
            uint32_t uVersion = 0;
            if (   pDirRec->bFileIdLength == 1
                && pDirRec->achFileId[0]  == '\0')
            {
                if (*pcbDirEntry < RT_UOFFSETOF(RTDIRENTRYEX, szName) + 2)
                {
                    *pcbDirEntry = RT_UOFFSETOF(RTDIRENTRYEX, szName) + 2;
                    Log3(("rtFsIsoDir_ReadDir9660: VERR_BUFFER_OVERFLOW (dot)\n"));
                    return VERR_BUFFER_OVERFLOW;
                }
                pDirEntry->cbName    = 1;
                pDirEntry->szName[0] = '.';
                pDirEntry->szName[1] = '\0';
            }
            else if (   pDirRec->bFileIdLength == 1
                     && pDirRec->achFileId[0]  == '\1')
            {
                if (*pcbDirEntry < RT_UOFFSETOF(RTDIRENTRYEX, szName) + 3)
                {
                    *pcbDirEntry = RT_UOFFSETOF(RTDIRENTRYEX, szName) + 3;
                    Log3(("rtFsIsoDir_ReadDir9660: VERR_BUFFER_OVERFLOW (dot-dot)\n"));
                    return VERR_BUFFER_OVERFLOW;
                }
                pDirEntry->cbName    = 2;
                pDirEntry->szName[0] = '.';
                pDirEntry->szName[1] = '.';
                pDirEntry->szName[2] = '\0';
            }
            else if (pShared->Core.pVol->fIsUtf16)
            {
                PCRTUTF16 pawcSrc   = (PCRTUTF16)&pDirRec->achFileId[0];
                size_t    cwcSrc    = pDirRec->bFileIdLength / sizeof(RTUTF16);
                size_t    cwcVer    = !(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY)
                                    ? rtFsIso9660GetVersionLengthUtf16Big(pawcSrc, cwcSrc, &uVersion) : 0;
                size_t    cchNeeded = 0;
                size_t    cbDst     = *pcbDirEntry - RT_UOFFSETOF(RTDIRENTRYEX, szName);
                char     *pszDst    = pDirEntry->szName;

                int rc = RTUtf16BigToUtf8Ex(pawcSrc, cwcSrc - cwcVer, &pszDst, cbDst, &cchNeeded);
                if (RT_SUCCESS(rc))
                    pDirEntry->cbName = (uint16_t)cchNeeded;
                else if (rc == VERR_BUFFER_OVERFLOW)
                {
                    *pcbDirEntry = RT_UOFFSETOF(RTDIRENTRYEX, szName) + cchNeeded + 1;
                    Log3(("rtFsIsoDir_ReadDir9660: VERR_BUFFER_OVERFLOW - cbDst=%zu cchNeeded=%zu (UTF-16BE)\n", cbDst, cchNeeded));
                    return VERR_BUFFER_OVERFLOW;
                }
                else
                {
                    ssize_t cchNeeded2 = RTStrPrintf2(pszDst, cbDst, "bad-name-%#x", pThis->offDir);
                    if (cchNeeded2 >= 0)
                        pDirEntry->cbName = (uint16_t)cchNeeded2;
                    else
                    {
                        *pcbDirEntry = RT_UOFFSETOF(RTDIRENTRYEX, szName) + (size_t)-cchNeeded2;
                        return VERR_BUFFER_OVERFLOW;
                    }
                }
            }
            else
            {
                /* This is supposed to be upper case ASCII, however, purge the encoding anyway. */
                size_t cchVer   = !(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY)
                                ? rtFsIso9660GetVersionLengthAscii(pDirRec->achFileId, pDirRec->bFileIdLength, &uVersion) : 0;
                size_t cchName  = pDirRec->bFileIdLength - cchVer;
                size_t cbNeeded = RT_UOFFSETOF(RTDIRENTRYEX, szName) + cchName + 1;
                if (*pcbDirEntry < cbNeeded)
                {
                    Log3(("rtFsIsoDir_ReadDir9660: VERR_BUFFER_OVERFLOW - cbDst=%zu cbNeeded=%zu (ASCII)\n", *pcbDirEntry, cbNeeded));
                    *pcbDirEntry = cbNeeded;
                    return VERR_BUFFER_OVERFLOW;
                }
                pDirEntry->cbName = (uint16_t)cchName;
                memcpy(pDirEntry->szName, pDirRec->achFileId, cchName);
                pDirEntry->szName[cchName] = '\0';
                RTStrPurgeEncoding(pDirEntry->szName);
            }
            pDirEntry->cwcShortName    = 0;
            pDirEntry->wszShortName[0] = '\0';

            /*
             * To avoid duplicating code in rtFsIsoCore_InitFrom9660DirRec and
             * rtFsIsoCore_QueryInfo, we create a dummy RTFSISOCORE on the stack.
             */
            RTFSISOCORE TmpObj;
            RT_ZERO(TmpObj);
            rtFsIsoCore_InitFrom9660DirRec(&TmpObj, pDirRec, 1 /* cDirRecs - see below why 1 */,
                                           pThis->offDir + pShared->Core.FirstExtent.off, uVersion, NULL, pShared->Core.pVol);
            int rc = rtFsIsoCore_QueryInfo(&TmpObj, &pDirEntry->Info, enmAddAttr);

            /*
             * Look for rock ridge info associated with this entry
             * and merge that into the record.
             */
            if (pRockInfo)
            {
                rtFsIsoDirShrd_ParseRockForDirRec(pShared, pDirRec, pRockInfo);
                if (pRockInfo->fValid)
                {
                    if (   pRockInfo->fSeenLastNM
                        && pRockInfo->cchName > 0
                        && !pShared->Core.pVol->fIsUtf16
                        && (   pDirRec->bFileIdLength != 1
                            || (   pDirRec->achFileId[0] != '\0'    /* . */
                                && pDirRec->achFileId[0] != '\1'))) /* .. */
                    {
                        size_t const cchName  = pRockInfo->cchName;
                        Assert(strlen(pRockInfo->szName) == cchName);
                        size_t const cbNeeded = RT_UOFFSETOF(RTDIRENTRYEX, szName) + cchName + 1;
                        if (*pcbDirEntry < cbNeeded)
                        {
                            Log3(("rtFsIsoDir_ReadDir9660: VERR_BUFFER_OVERFLOW - cbDst=%zu cbNeeded=%zu (Rock)\n", *pcbDirEntry, cbNeeded));
                            *pcbDirEntry = cbNeeded;
                            return VERR_BUFFER_OVERFLOW;
                        }
                        pDirEntry->cbName = (uint16_t)cchName;
                        memcpy(pDirEntry->szName, pRockInfo->szName, cchName);
                        pDirEntry->szName[cchName] = '\0';

                        RTStrPurgeEncoding(pDirEntry->szName);
                    }
                }
            }

            /*
             * Update the directory location and handle multi extent records.
             *
             * Multi extent records only affect the file size and the directory location,
             * so we deal with it here instead of involving rtFsIsoCore_InitFrom9660DirRec
             * which would potentially require freeing memory and such.
             */
            if (!(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT))
            {
                Log3(("rtFsIsoDir_ReadDir9660: offDir=%#07x: %s (rc=%Rrc)\n", pThis->offDir, pDirEntry->szName, rc));
                pThis->offDir += pDirRec->cbDirRec;
            }
            else
            {
                uint32_t cExtents = 1;
                uint32_t offDir   = pThis->offDir + pDirRec->cbDirRec;
                while (offDir + RT_UOFFSETOF(ISO9660DIRREC, achFileId) <= pShared->cbDir)
                {
                    PCISO9660DIRREC pDirRec2 = (PCISO9660DIRREC)&pShared->pbDir[offDir];
                    if (pDirRec2->cbDirRec != 0)
                    {
                        pDirEntry->Info.cbObject += ISO9660_GET_ENDIAN(&pDirRec2->cbData);
                        offDir += pDirRec2->cbDirRec;
                        cExtents++;
                        if (!(pDirRec2->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT))
                            break;
                    }
                    else
                        offDir = (offDir + pShared->Core.pVol->cbSector) & ~(pShared->Core.pVol->cbSector - 1U);
                }
                Log3(("rtFsIsoDir_ReadDir9660: offDir=%#07x, %u extents ending at %#07x: %s (rc=%Rrc)\n",
                      pThis->offDir, cExtents, offDir, pDirEntry->szName, rc));
                pThis->offDir = offDir;
            }

            return rc;
        }
    }

    Log3(("rtFsIsoDir_ReadDir9660: offDir=%#07x: VERR_NO_MORE_FILES\n", pThis->offDir));
    return VERR_NO_MORE_FILES;
}


/**
 * The UDF worker for rtFsIsoDir_ReadDir
 */
static int rtFsIsoDir_ReadDirUdf(PRTFSISODIROBJ pThis, PRTFSISODIRSHRD pShared, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                                 RTFSOBJATTRADD enmAddAttr)
{
    /*
     * At offset zero we've got the '.' entry.  This has to be generated
     * manually as it's not part of the directory content.  The directory
     * offset has to be faked for this too, so offDir == 0 indicates the '.'
     * entry whereas offDir == 1 is the first file id descriptor.
     */
    if (pThis->offDir == 0)
    {
        if (*pcbDirEntry < RT_UOFFSETOF(RTDIRENTRYEX, szName) + 2)
        {
            *pcbDirEntry = RT_UOFFSETOF(RTDIRENTRYEX, szName) + 2;
            Log3(("rtFsIsoDir_ReadDirUdf: VERR_BUFFER_OVERFLOW (dot)\n"));
            return VERR_BUFFER_OVERFLOW;
        }
        pDirEntry->cbName    = 1;
        pDirEntry->szName[0] = '.';
        pDirEntry->szName[1] = '\0';
        pDirEntry->cwcShortName = 0;
        pDirEntry->wszShortName[0] = '\0';

        int rc = rtFsIsoCore_QueryInfo(&pShared->Core, &pDirEntry->Info, enmAddAttr);

        Log3(("rtFsIsoDir_ReadDirUdf: offDir=%#07x: %s (rc=%Rrc)\n", pThis->offDir, pDirEntry->szName, rc));
        pThis->offDir = 1;
        return rc;
    }

    /*
     * Do the directory content.
     */
    while (pThis->offDir + RT_UOFFSETOF(UDFFILEIDDESC, abImplementationUse) <= pShared->cbDir + 1) /* see above for +1 expl. */
    {
        PCUDFFILEIDDESC pFid  = (PCUDFFILEIDDESC)&pShared->pbDir[pThis->offDir - 1];
        uint32_t const  cbFid = UDFFILEIDDESC_GET_SIZE(pFid);

        if (pThis->offDir + cbFid <= pShared->cbDir + 1)
        { /* likely */ }
        else
            break;

        /*
         * Do names first as they may cause overflows.
         */
        if (pFid->cbName > 1)
        {
            uint8_t const  *pbName = UDFFILEIDDESC_2_NAME(pFid);
            uint32_t        cbSrc  = pFid->cbName;
            if (*pbName == 8)
            {
                /* Figure out the UTF-8 length first. */
                bool     fSimple = true;
                uint32_t cchDst  = 0;
                for (uint32_t offSrc = 1; offSrc < cbSrc; offSrc++)
                    if (!(pbName[offSrc] & 0x80))
                        cchDst++;
                    else
                    {
                        cchDst += 2;
                        fSimple = false;
                    }

                size_t cbNeeded = RT_UOFFSETOF(RTDIRENTRYEX, szName) + cchDst + 1;
                if (*pcbDirEntry >= cbNeeded)
                {
                    if (fSimple)
                    {
                        Assert(cbSrc - 1 == cchDst);
                        memcpy(pDirEntry->szName, &pbName[1], cchDst);
                        pDirEntry->szName[cchDst] = '\0';
                    }
                    else
                    {
                        char *pszDst = pDirEntry->szName;
                        for (uint32_t offSrc = 1; offSrc < cbSrc; offSrc++)
                            pszDst = RTStrPutCp(pszDst, pbName[offSrc]);
                        *pszDst = '\0';
                        Assert((size_t)(pszDst - &pDirEntry->szName[0]) == cchDst);
                    }
                }
                else
                {
                    Log3(("rtFsIsoDir_ReadDirUdf: VERR_BUFFER_OVERFLOW - cbDst=%zu cbNeeded=%zu (8-bit)\n", *pcbDirEntry, cbNeeded));
                    *pcbDirEntry = cbNeeded;
                    return VERR_BUFFER_OVERFLOW;
                }
            }
            else
            {
                /* Let RTUtf16BigToUtf8Ex do the bounds checking. */
                char  *pszDst    = pDirEntry->szName;
                size_t cbDst     = *pcbDirEntry - RT_UOFFSETOF(RTDIRENTRYEX, szName);
                size_t cchNeeded = 0;
                int    rc;
                if (*pbName == 16)
                    rc = RTUtf16BigToUtf8Ex((PCRTUTF16)(pbName + 1), (cbSrc - 1) / sizeof(RTUTF16), &pszDst, cbDst, &cchNeeded);
                else
                    rc = VERR_INVALID_NAME;
                if (RT_SUCCESS(rc))
                    pDirEntry->cbName = (uint16_t)cchNeeded;
                else if (rc == VERR_BUFFER_OVERFLOW)
                {
                    *pcbDirEntry = RT_UOFFSETOF(RTDIRENTRYEX, szName) + cchNeeded + 1;
                    Log3(("rtFsIsoDir_ReadDirUdf: VERR_BUFFER_OVERFLOW - cbDst=%zu cchNeeded=%zu (16-bit)\n", cbDst, cchNeeded));
                    return VERR_BUFFER_OVERFLOW;
                }
                else
                {
                    LogRelMax(90, ("ISO/UDF: Malformed directory entry name at %#x: %.*Rhxs\n", pThis->offDir - 1, cbSrc, pbName));
                    ssize_t cchNeeded2 = RTStrPrintf2(pszDst, cbDst, "bad-name-%#x", pThis->offDir - 1);
                    if (cchNeeded2 >= 0)
                        pDirEntry->cbName = (uint16_t)cchNeeded2;
                    else
                    {
                        *pcbDirEntry = RT_UOFFSETOF(RTDIRENTRYEX, szName) + (size_t)-cchNeeded2;
                        return VERR_BUFFER_OVERFLOW;
                    }
                }
            }
        }
        else if (pFid->fFlags & UDF_FILE_FLAGS_PARENT)
        {
            size_t cbNeeded = RT_UOFFSETOF(RTDIRENTRYEX, szName) + 2 + 1;
            if (*pcbDirEntry < cbNeeded)
            {
                Log3(("rtFsIsoDir_ReadDirUdf: VERR_BUFFER_OVERFLOW - cbDst=%zu cbNeeded=%zu (dot-dot)\n", *pcbDirEntry, cbNeeded));
                *pcbDirEntry = cbNeeded;
                return VERR_BUFFER_OVERFLOW;
            }
            pDirEntry->cbName    = 2;
            pDirEntry->szName[0] = '.';
            pDirEntry->szName[1] = '.';
            pDirEntry->szName[2] = '\0';
        }
        else
        {
            size_t cbNeeded = RT_UOFFSETOF(RTDIRENTRYEX, szName) + 1;
            if (*pcbDirEntry < cbNeeded)
            {
                Log3(("rtFsIsoDir_ReadDirUdf: VERR_BUFFER_OVERFLOW - cbDst=%zu cbNeeded=%zu (empty)\n", *pcbDirEntry, cbNeeded));
                *pcbDirEntry = cbNeeded;
                return VERR_BUFFER_OVERFLOW;
            }
            pDirEntry->cbName    = 0;
            pDirEntry->szName[0] = '\0';
        }

        pDirEntry->cwcShortName    = 0;
        pDirEntry->wszShortName[0] = '\0';

        /*
         * To avoid duplicating code in rtFsIsoCore_InitUdf and
         * rtFsIsoCore_QueryInfo, we create a dummy RTFSISOCORE on the stack.
         */
        RTFSISOCORE TmpObj;
        RT_ZERO(TmpObj);
        int rc = rtFsIsoCore_InitFromUdfIcbAndFileIdDesc(&TmpObj, &pFid->Icb, pFid, pThis->offDir - 1, pShared->Core.pVol);
        if (RT_SUCCESS(rc))
        {
            rc = rtFsIsoCore_QueryInfo(&TmpObj, &pDirEntry->Info, enmAddAttr);
            rtFsIsoCore_Destroy(&TmpObj);
        }

        /*
         * Update.
         */
        Log3(("rtFsIsoDir_ReadDirUdf: offDir=%#07x: %s (rc=%Rrc)\n", pThis->offDir, pDirEntry->szName, rc));
        pThis->offDir += cbFid;

        return rc;
    }

    Log3(("rtFsIsoDir_ReadDirUdf: offDir=%#07x: VERR_NO_MORE_FILES\n", pThis->offDir));
    return VERR_NO_MORE_FILES;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnReadDir}
 */
static DECLCALLBACK(int) rtFsIsoDir_ReadDir(void *pvThis, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                                            RTFSOBJATTRADD enmAddAttr)
{
    PRTFSISODIROBJ  pThis   = (PRTFSISODIROBJ)pvThis;
    PRTFSISODIRSHRD pShared = pThis->pShared;
    int rc;
    if (pShared->Core.pVol->enmType != RTFSISOVOLTYPE_UDF)
        rc = rtFsIsoDir_ReadDir9660(pThis, pShared, pDirEntry, pcbDirEntry, enmAddAttr);
    else
        rc = rtFsIsoDir_ReadDirUdf(pThis, pShared, pDirEntry, pcbDirEntry, enmAddAttr);
    return rc;
}


/**
 * ISO file operations.
 */
static const RTVFSDIROPS g_rtFsIsoDirOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_DIR,
        "ISO 9660 Dir",
        rtFsIsoDir_Close,
        rtFsIsoDir_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSDIROPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSDIROPS, ObjSet) - RT_UOFFSETOF(RTVFSDIROPS, Obj),
        NULL /*SetMode*/,
        NULL /*SetTimes*/,
        NULL /*SetOwner*/,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsIsoDir_Open,
    NULL /* pfnFollowAbsoluteSymlink */,
    NULL /* pfnOpenFile */,
    NULL /* pfnOpenDir */,
    rtFsIsoDir_CreateDir,
    rtFsIsoDir_OpenSymlink,
    rtFsIsoDir_CreateSymlink,
    NULL /* pfnQueryEntryInfo */,
    rtFsIsoDir_UnlinkEntry,
    rtFsIsoDir_RenameEntry,
    rtFsIsoDir_RewindDir,
    rtFsIsoDir_ReadDir,
    RTVFSDIROPS_VERSION,
};


/**
 * Adds an open child to the parent directory's shared structure.
 *
 * Maintains an additional reference to the parent dir to prevent it from going
 * away.  If @a pDir is the root directory, it also ensures the volume is
 * referenced and sticks around until the last open object is gone.
 *
 * @param   pDir        The directory.
 * @param   pChild      The child being opened.
 * @sa      rtFsIsoDirShrd_RemoveOpenChild
 */
static void rtFsIsoDirShrd_AddOpenChild(PRTFSISODIRSHRD pDir, PRTFSISOCORE pChild)
{
    rtFsIsoDirShrd_Retain(pDir);

    RTListAppend(&pDir->OpenChildren, &pChild->Entry);
    pChild->pParentDir = pDir;
}


/**
 * Removes an open child to the parent directory.
 *
 * @param   pDir        The directory.
 * @param   pChild      The child being removed.
 *
 * @remarks This is the very last thing you do as it may cause a few other
 *          objects to be released recursively (parent dir and the volume).
 *
 * @sa      rtFsIsoDirShrd_AddOpenChild
 */
static void rtFsIsoDirShrd_RemoveOpenChild(PRTFSISODIRSHRD pDir, PRTFSISOCORE pChild)
{
    AssertReturnVoid(pChild->pParentDir == pDir);
    RTListNodeRemove(&pChild->Entry);
    pChild->pParentDir = NULL;

    rtFsIsoDirShrd_Release(pDir);
}


#ifdef LOG_ENABLED
/**
 * Logs the content of a directory.
 */
static void rtFsIsoDirShrd_Log9660Content(PRTFSISODIRSHRD pThis)
{
    if (LogIs2Enabled())
    {
        uint32_t offRec = 0;
        while (offRec < pThis->cbDir)
        {
            PCISO9660DIRREC pDirRec = (PCISO9660DIRREC)&pThis->pbDir[offRec];
            if (pDirRec->cbDirRec == 0)
                break;

            RTUTF16 wszName[128];
            if (pThis->Core.pVol->fIsUtf16)
            {
                PRTUTF16  pwszDst = &wszName[pDirRec->bFileIdLength / sizeof(RTUTF16)];
                PCRTUTF16 pwszSrc = (PCRTUTF16)&pDirRec->achFileId[pDirRec->bFileIdLength];
                pwszSrc--;
                *pwszDst-- = '\0';
                while ((uintptr_t)pwszDst >= (uintptr_t)&wszName[0])
                {
                    *pwszDst = RT_BE2H_U16(*pwszSrc);
                    pwszDst--;
                    pwszSrc--;
                }
            }
            else
            {
                PRTUTF16 pwszDst = wszName;
                for (uint32_t off = 0; off < pDirRec->bFileIdLength; off++)
                    *pwszDst++ = pDirRec->achFileId[off];
                *pwszDst = '\0';
            }

            Log2(("ISO9660:  %04x: rec=%#x ea=%#x cb=%#010RX32 off=%#010RX32 fl=%#04x %04u-%02u-%02u %02u:%02u:%02u%+03d unit=%#x igap=%#x idVol=%#x '%ls'\n",
                  offRec,
                  pDirRec->cbDirRec,
                  pDirRec->cExtAttrBlocks,
                  ISO9660_GET_ENDIAN(&pDirRec->cbData),
                  ISO9660_GET_ENDIAN(&pDirRec->offExtent),
                  pDirRec->fFileFlags,
                  pDirRec->RecTime.bYear + 1900,
                  pDirRec->RecTime.bMonth,
                  pDirRec->RecTime.bDay,
                  pDirRec->RecTime.bHour,
                  pDirRec->RecTime.bMinute,
                  pDirRec->RecTime.bSecond,
                  pDirRec->RecTime.offUtc*4/60,
                  pDirRec->bFileUnitSize,
                  pDirRec->bInterleaveGapSize,
                  ISO9660_GET_ENDIAN(&pDirRec->VolumeSeqNo),
                  wszName));

            uint32_t offSysUse = RT_UOFFSETOF_DYN(ISO9660DIRREC, achFileId[pDirRec->bFileIdLength])
                               + !(pDirRec->bFileIdLength & 1);
            if (offSysUse < pDirRec->cbDirRec)
            {
                Log2(("ISO9660:       system use (%#x bytes):\n%.*RhxD\n", pDirRec->cbDirRec - offSysUse,
                      pDirRec->cbDirRec - offSysUse, (uint8_t *)pDirRec + offSysUse));
            }

            /* advance */
            offRec += pDirRec->cbDirRec;
        }
    }
}
#endif /* LOG_ENABLED */


/**
 * Instantiates a new shared directory structure, given 9660 records.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO volume instance.
 * @param   pParentDir      The parent directory.  This is NULL for the root
 *                          directory.
 * @param   pDirRec         The directory record.  Will access @a cDirRecs
 *                          records.
 * @param   cDirRecs        Number of directory records if more than one.
 * @param   offDirRec       The byte offset of the directory record.
 * @param   pRockInfo       Optional pointer to rock ridge info for the entry.
 * @param   ppShared        Where to return the shared directory structure.
 */
static int rtFsIsoDirShrd_New9660(PRTFSISOVOL pThis, PRTFSISODIRSHRD pParentDir, PCISO9660DIRREC pDirRec,
                                  uint32_t cDirRecs, uint64_t offDirRec, PCRTFSISOROCKINFO pRockInfo, PRTFSISODIRSHRD *ppShared)
{
    /*
     * Allocate a new structure and initialize it.
     */
    int rc = VERR_NO_MEMORY;
    PRTFSISODIRSHRD pShared = (PRTFSISODIRSHRD)RTMemAllocZ(sizeof(*pShared));
    if (pShared)
    {
        rc = rtFsIsoCore_InitFrom9660DirRec(&pShared->Core, pDirRec, cDirRecs, offDirRec, 0 /*uVersion*/, pRockInfo, pThis);
        if (RT_SUCCESS(rc))
        {
            RTListInit(&pShared->OpenChildren);
            pShared->cbDir = ISO9660_GET_ENDIAN(&pDirRec->cbData);
            pShared->pbDir = (uint8_t *)RTMemAllocZ(pShared->cbDir + 256);
            if (pShared->pbDir)
            {
                rc = RTVfsFileReadAt(pThis->hVfsBacking, pShared->Core.FirstExtent.off, pShared->pbDir, pShared->cbDir, NULL);
                if (RT_SUCCESS(rc))
                {
#ifdef LOG_ENABLED
                    rtFsIsoDirShrd_Log9660Content(pShared);
#endif

                    /*
                     * If this is the root directory, check if rock ridge info is present.
                     */
                    if (   !pParentDir
                        && !(pThis->fFlags & RTFSISO9660_F_NO_ROCK)
                        && pShared->cbDir > RT_UOFFSETOF(ISO9660DIRREC, achFileId[1]))
                    {
                        PCISO9660DIRREC pDirRec0 = (PCISO9660DIRREC)pShared->pbDir;
                        if (   pDirRec0->bFileIdLength == 1
                            && pDirRec0->achFileId[0]  == 0
                            && pDirRec0->cbDirRec > RT_UOFFSETOF(ISO9660DIRREC, achFileId[1]))
                            rtFsIsoDirShrd_ParseRockForRoot(pShared, pDirRec0);
                    }

                    /*
                     * Link into parent directory so we can use it to update
                     * our directory entry.
                     */
                    if (pParentDir)
                        rtFsIsoDirShrd_AddOpenChild(pParentDir, &pShared->Core);
                    *ppShared = pShared;
                    return VINF_SUCCESS;
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
        RTMemFree(pShared);
    }
    *ppShared = NULL;
    return rc;
}


/**
 * Instantiates a new shared directory structure, given UDF descriptors.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO volume instance.
 * @param   pParentDir      The parent directory.  This is NULL for the root
 *                          directory.
 * @param   pAllocDesc      The allocation descriptor for the directory ICB.
 * @param   pFileIdDesc     The file ID descriptor.  This is NULL for the root.
 * @param   offInDir        The offset of the file ID descriptor in the parent
 *                          directory.  This is used when  looking up shared
 *                          directory objects.  (Pass 0 for root.)
 * @param   ppShared        Where to return the shared directory structure.
 */
static int rtFsIsoDirShrd_NewUdf(PRTFSISOVOL pThis, PRTFSISODIRSHRD pParentDir, PCUDFLONGAD pAllocDesc,
                                 PCUDFFILEIDDESC pFileIdDesc, uintptr_t offInDir, PRTFSISODIRSHRD *ppShared)
{
    /*
     * Allocate a new structure and initialize it.
     */
    int rc = VERR_NO_MEMORY;
    PRTFSISODIRSHRD pShared = (PRTFSISODIRSHRD)RTMemAllocZ(sizeof(*pShared));
    if (pShared)
    {
        rc = rtFsIsoCore_InitFromUdfIcbAndFileIdDesc(&pShared->Core, pAllocDesc, pFileIdDesc, offInDir, pThis);
        if (RT_SUCCESS(rc))
        {
            RTListInit(&pShared->OpenChildren);

            if (pShared->Core.cbObject < RTFSISO_MAX_DIR_SIZE)
            {
                pShared->cbDir = (uint32_t)pShared->Core.cbObject;
                pShared->pbDir = (uint8_t *)RTMemAllocZ(RT_MAX(RT_ALIGN_32(pShared->cbDir, 512), 512));
                if (pShared->pbDir)
                {
                    rc = rtFsIsoCore_ReadWorker(&pShared->Core, 0, pShared->pbDir, pShared->cbDir, NULL, NULL);
                    if (RT_SUCCESS(rc))
                    {
#ifdef LOG_ENABLED
                        RTFsUdfHlpLogDirContent(pShared->pbDir, pShared->cbDir);
#endif

                        /*
                         * Link into parent directory so we can use it to update
                         * our directory entry.
                         */
                        if (pParentDir)
                            rtFsIsoDirShrd_AddOpenChild(pParentDir, &pShared->Core);
                        *ppShared = pShared;
                        return VINF_SUCCESS;
                    }
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }
        RTMemFree(pShared);
    }

    *ppShared = NULL;
    return rc;
}


/**
 * Instantiates a new directory with a shared structure presupplied.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO volume instance.
 * @param   pShared         Referenced pointer to the shared structure.  The
 *                          reference is always CONSUMED.
 * @param   phVfsDir        Where to return the directory handle.
 */
static int rtFsIsoDir_NewWithShared(PRTFSISOVOL pThis, PRTFSISODIRSHRD pShared, PRTVFSDIR phVfsDir)
{
    /*
     * Create VFS object around the shared structure.
     */
    PRTFSISODIROBJ pNewDir;
    int rc = RTVfsNewDir(&g_rtFsIsoDirOps, sizeof(*pNewDir), 0 /*fFlags*/, pThis->hVfsSelf,
                         NIL_RTVFSLOCK /*use volume lock*/, phVfsDir, (void **)&pNewDir);
    if (RT_SUCCESS(rc))
    {
        /*
         * Look for existing shared object, create a new one if necessary.
         * We CONSUME a reference to pShared here.
         */
        pNewDir->offDir  = 0;
        pNewDir->pShared = pShared;
        return VINF_SUCCESS;
    }

    rtFsIsoDirShrd_Release(pShared);
    *phVfsDir = NIL_RTVFSDIR;
    return rc;
}



/**
 * Instantiates a new directory VFS instance for ISO 9660, creating the shared
 * structure as necessary.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO volume instance.
 * @param   pParentDir      The parent directory.  This is NULL for the root
 *                          directory.
 * @param   pDirRec         The directory record.
 * @param   cDirRecs        Number of directory records if more than one.
 * @param   offDirRec       The byte offset of the directory record.
 * @param   pRockInfo       Optional pointer to rock ridge info for the entry.
 * @param   phVfsDir        Where to return the directory handle.
 */
static int  rtFsIsoDir_New9660(PRTFSISOVOL pThis, PRTFSISODIRSHRD pParentDir, PCISO9660DIRREC pDirRec,
                               uint32_t cDirRecs, uint64_t offDirRec, PCRTFSISOROCKINFO pRockInfo, PRTVFSDIR phVfsDir)
{
    /*
     * Look for existing shared object, create a new one if necessary.
     */
    PRTFSISODIRSHRD pShared = (PRTFSISODIRSHRD)rtFsIsoDir_LookupShared(pParentDir, offDirRec);
    if (!pShared)
    {
        int rc = rtFsIsoDirShrd_New9660(pThis, pParentDir, pDirRec, cDirRecs, offDirRec, pRockInfo, &pShared);
        if (RT_FAILURE(rc))
        {
            *phVfsDir = NIL_RTVFSDIR;
            return rc;
        }
    }
    return rtFsIsoDir_NewWithShared(pThis, pShared, phVfsDir);
}


/**
 * Instantiates a new directory VFS instance for UDF, creating the shared
 * structure as necessary.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO volume instance.
 * @param   pParentDir      The parent directory.
 * @param   pFid            The file ID descriptor for the directory.
 * @param   phVfsDir        Where to return the directory handle.
 */
static int  rtFsIsoDir_NewUdf(PRTFSISOVOL pThis, PRTFSISODIRSHRD pParentDir, PCUDFFILEIDDESC pFid, PRTVFSDIR phVfsDir)
{
    Assert(pFid);
    Assert(pParentDir);
    uintptr_t const offInDir = (uintptr_t)pFid - (uintptr_t)pParentDir->pbDir;
    Assert(offInDir < pParentDir->cbDir);

    /*
     * Look for existing shared object, create a new one if necessary.
     */
    PRTFSISODIRSHRD pShared = (PRTFSISODIRSHRD)rtFsIsoDir_LookupShared(pParentDir, offInDir);
    if (!pShared)
    {
        int rc = rtFsIsoDirShrd_NewUdf(pThis, pParentDir, &pFid->Icb, pFid, offInDir, &pShared);
        if (RT_FAILURE(rc))
        {
            *phVfsDir = NIL_RTVFSDIR;
            return rc;
        }
    }
    return rtFsIsoDir_NewWithShared(pThis, pShared, phVfsDir);
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnClose}
 */
static DECLCALLBACK(int) rtFsIsoVol_Close(void *pvThis)
{
    PRTFSISOVOL pThis = (PRTFSISOVOL)pvThis;
    Log(("rtFsIsoVol_Close(%p)\n", pThis));

    if (pThis->pRootDir)
    {
        Assert(RTListIsEmpty(&pThis->pRootDir->OpenChildren));
        Assert(pThis->pRootDir->Core.cRefs == 1);
        rtFsIsoDirShrd_Release(pThis->pRootDir);
        pThis->pRootDir = NULL;
    }

    RTFsUdfHlpDestroyVolInfo(&pThis->Udf.VolInfo);

    RTVfsFileRelease(pThis->hVfsBacking);
    pThis->hVfsBacking = NIL_RTVFSFILE;

    if (RTCritSectIsInitialized(&pThis->RockBufLock))
        RTCritSectDelete(&pThis->RockBufLock);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsIsoVol_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RT_NOREF(pvThis, pObjInfo, enmAddAttr);
    return VERR_WRONG_TYPE;
}


/**
 * For now this is a sanitized version of rtFsIsoVolGetMaybeUtf16Be, which is
 * probably not correct or anything, but will have to do for now.
 */
static int rtFsIsoVol_ReturnIso9660D1String(const char *pachSrc, size_t cchSrc, void *pvDst, size_t cbDst, size_t *pcbRet)
{
    char *pszDst = (char *)pvDst;

    /*
     * Check if it may be some UTF16 variant by scanning for zero bytes
     * (ISO-9660 doesn't allow zeros).
     */
    size_t cFirstZeros  = 0;
    size_t cSecondZeros = 0;
    for (size_t off = 0; off + 1 < cchSrc; off += 2)
    {
        cFirstZeros  += pachSrc[off]     == '\0';
        cSecondZeros += pachSrc[off + 1] == '\0';
    }
    if (cFirstZeros > cSecondZeros)
    {
        /*
         * UTF-16BE / UTC-2BE:
         */
        if (cchSrc & 1)
        {
            AssertReturn(pachSrc[cchSrc - 1] == '\0' || pachSrc[cchSrc - 1] == ' ', VERR_INVALID_UTF16_ENCODING);
            cchSrc--;
        }
        while (   cchSrc >= 2
               && pachSrc[cchSrc - 1] == ' '
               && pachSrc[cchSrc - 2] == '\0')
            cchSrc -= 2;

        if (cbDst > 0)
            return RTUtf16BigToUtf8Ex((PCRTUTF16)pachSrc, cchSrc / sizeof(RTUTF16), &pszDst, cbDst, pcbRet);
        int rc = RTUtf16BigCalcUtf8LenEx((PCRTUTF16)pachSrc, cchSrc / sizeof(RTUTF16), pcbRet);
        if (RT_SUCCESS(rc))
        {
            *pcbRet += 1;
            return VERR_BUFFER_OVERFLOW;
        }
        return rc;
    }

    if (cSecondZeros > 0)
    {
        /*
         * Little endian UTF-16 / UCS-2.
         */
        if (cchSrc & 1)
        {
            AssertReturn(pachSrc[cchSrc - 1] == '\0' || pachSrc[cchSrc - 1] == ' ', VERR_INVALID_UTF16_ENCODING);
            cchSrc--;
        }
        while (   cchSrc >= 2
               && pachSrc[cchSrc - 1] == '\0'
               && pachSrc[cchSrc - 2] == ' ')
            cchSrc -= 2;

        if (cbDst)
            return RTUtf16LittleToUtf8Ex((PCRTUTF16)pachSrc, cchSrc / sizeof(RTUTF16), &pszDst, cbDst, pcbRet);
        int rc = RTUtf16LittleCalcUtf8LenEx((PCRTUTF16)pachSrc, cchSrc / sizeof(RTUTF16), pcbRet);
        if (RT_SUCCESS(rc))
        {
            *pcbRet += 1;
            return VERR_BUFFER_OVERFLOW;
        }
        return rc;
    }

    /*
     * ASSUME UTF-8/ASCII.
     */
    while (   cchSrc > 0
           && pachSrc[cchSrc - 1] == ' ')
        cchSrc--;

    *pcbRet = cchSrc + 1;
    int rc = RTStrCopyEx(pszDst, cbDst, pachSrc, cchSrc);
    if (cbDst > 0)
        RTStrPurgeEncoding(pszDst);
    return rc;
}


static int rtFsIsoVol_ReturnIso9660DString(const char *pachSrc, size_t cchSrc, void *pvDst, size_t cbDst, size_t *pcbRet)
{
    /* Lazy bird: */
    return rtFsIsoVol_ReturnIso9660D1String(pachSrc, cchSrc, pvDst, cbDst, pcbRet);
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryInfoEx}
 */
static DECLCALLBACK(int) rtFsIsoVol_QueryInfoEx(void *pvThis, RTVFSQIEX enmInfo, void *pvInfo, size_t cbInfo, size_t *pcbRet)
{
    PRTFSISOVOL pThis = (PRTFSISOVOL)pvThis;
    LogFlow(("rtFsIsoVol_QueryInfo(%p, %d,, %#zx,)\n", pThis, enmInfo, cbInfo));

    union
    {
        uint8_t                 ab[RTFSISO_MAX_LOGICAL_BLOCK_SIZE];
        ISO9660PRIMARYVOLDESC   PriVolDesc;
        ISO9660SUPVOLDESC       SupVolDesc;
    } uBuf;

    switch (enmInfo)
    {
        case RTVFSQIEX_VOL_LABEL:
        case RTVFSQIEX_VOL_LABEL_ALT:
        {
            if (   pThis->enmType == RTFSISOVOLTYPE_UDF
                && (   enmInfo == RTVFSQIEX_VOL_LABEL
                    || pThis->offPrimaryVolDesc == 0))
                return RTFsUdfHlpDStringFieldToUtf8Buf(pThis->Udf.VolInfo.achLogicalVolumeID,
                                                       sizeof(pThis->Udf.VolInfo.achLogicalVolumeID), pvInfo, cbInfo, pcbRet);

            bool const fPrimary = enmInfo == RTVFSQIEX_VOL_LABEL_ALT
                               || pThis->enmType == RTFSISOVOLTYPE_ISO9960;

            int rc = RTVfsFileReadAt(pThis->hVfsBacking,
                                     fPrimary ? pThis->offPrimaryVolDesc : pThis->offSecondaryVolDesc,
                                     uBuf.ab, RT_MAX(RT_MIN(pThis->cbSector, sizeof(uBuf)), sizeof(uBuf.PriVolDesc)), NULL);
            AssertRCReturn(rc, rc);

            if (fPrimary)
                return rtFsIsoVol_ReturnIso9660DString(uBuf.PriVolDesc.achVolumeId, sizeof(uBuf.PriVolDesc.achVolumeId),
                                                       pvInfo, cbInfo, pcbRet);
            return rtFsIsoVol_ReturnIso9660D1String(uBuf.SupVolDesc.achVolumeId, sizeof(uBuf.SupVolDesc.achVolumeId),
                                                    pvInfo, cbInfo, pcbRet);
        }

        default:
            return VERR_NOT_SUPPORTED;

    }
}


/**
 * @interface_method_impl{RTVFSOPS,pfnOpenRoot}
 */
static DECLCALLBACK(int) rtFsIsoVol_OpenRoot(void *pvThis, PRTVFSDIR phVfsDir)
{
    PRTFSISOVOL pThis = (PRTFSISOVOL)pvThis;

    rtFsIsoDirShrd_Retain(pThis->pRootDir); /* consumed by the next call */
    return rtFsIsoDir_NewWithShared(pThis, pThis->pRootDir, phVfsDir);
}


/**
 * @interface_method_impl{RTVFSOPS,pfnQueryRangeState}
 */
static DECLCALLBACK(int) rtFsIsoVol_QueryRangeState(void *pvThis, uint64_t off, size_t cb, bool *pfUsed)
{
    RT_NOREF(pvThis, off, cb, pfUsed);
    return VERR_NOT_IMPLEMENTED;
}


DECL_HIDDEN_CONST(const RTVFSOPS) g_rtFsIsoVolOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_VFS,
        "ISO 9660/UDF",
        rtFsIsoVol_Close,
        rtFsIsoVol_QueryInfo,
        rtFsIsoVol_QueryInfoEx,
        RTVFSOBJOPS_VERSION
    },
    RTVFSOPS_VERSION,
    0 /* fFeatures */,
    rtFsIsoVol_OpenRoot,
    rtFsIsoVol_QueryRangeState,
    RTVFSOPS_VERSION
};


/**
 * Goes looking for UDF when we've seens a volume recognition sequence.
 *
 * @returns IPRT status code.
 * @param   pThis               The volume instance data.
 * @param   puUdfLevel          The UDF level indicated by the VRS.
 * @param   offUdfBootVolDesc   The offset of the BOOT2 descriptor, UINT64_MAX
 *                              if not encountered.
 * @param   pbBuf               Buffer for reading into.
 * @param   cbBuf               The size of the buffer.  At least one sector.
 * @param   pErrInfo            Where to return extended error info.
 */
static int rtFsIsoVolHandleUdfDetection(PRTFSISOVOL pThis, uint8_t *puUdfLevel, uint64_t offUdfBootVolDesc,
                                        uint8_t *pbBuf, size_t cbBuf, PRTERRINFO pErrInfo)
{
    NOREF(offUdfBootVolDesc);

    /*
     * There are up to three anchor volume descriptor pointers that can give us
     * two different descriptor sequences each.  Usually, the different AVDP
     * structures points to the same two sequences.  The idea here is that
     * sectors may deteriorate and become unreadable, and we're supposed to try
     * out alternative sectors to get the job done.  If we really took this
     * seriously, we could try read all sequences in parallel and use the
     * sectors that are good.  However, we'll try keep things reasonably simple
     * since we'll most likely be reading from hard disks rather than optical
     * media.
     *
     * We keep track of which sequences we've processed so we don't try to do it
     * again when alternative AVDP sectors points to the same sequences.
     */
    RTFSUDFHLPCTX Ctx;
    Ctx.hVfsBacking         = pThis->hVfsBacking;
    Ctx.cbBacking           = pThis->cbBacking;
    Ctx.cBackingSectors     = pThis->cBackingSectors;
    Ctx.cbSector            = pThis->cbSector;
    Ctx.cbMaxLogicalBlock   = RTFSISO_MAX_LOGICAL_BLOCK_SIZE;
    Ctx.pbBuf               = pbBuf;
    Ctx.cbBuf               = cbBuf;
    Ctx.pErrInfo            = pErrInfo;

    pThis->Udf.uLevel = *puUdfLevel;
    RTFSUDFHLPSEENSEQENCES SeenSequences = {0};
    int rc1 = RTFsUdfHlpReadAndHandleUdfAvdp(&Ctx, 256 * pThis->cbSector, &SeenSequences, &pThis->Udf.VolInfo);
    if (RT_SUCCESS(rc1))
        return rc1;

    int rc2 = RTFsUdfHlpReadAndHandleUdfAvdp(&Ctx,  pThis->cbBacking - 256 * pThis->cbSector, &SeenSequences, &pThis->Udf.VolInfo);
    if (RT_SUCCESS(rc2))
        return rc2;

    int rc3 = RTFsUdfHlpReadAndHandleUdfAvdp(&Ctx,  pThis->cbBacking - pThis->cbSector, &SeenSequences, &pThis->Udf.VolInfo);
    if (RT_SUCCESS(rc3))
        return rc3;

    /*
     * Return failure if the alternatives have been excluded.
     *
     * Note! The error info won't be correct here.
     */
    pThis->Udf.uLevel = *puUdfLevel = 0;

    if (   (pThis->fFlags & RTFSISO9660_F_ONLY_UDF)
        || RTFSISO9660_F_IS_ONLY_TYPE(pThis->fFlags, RTFSISO9660_F_NO_UDF) )
        return rc1 != VERR_NOT_FOUND ? rc1 : rc2 != VERR_NOT_FOUND ? rc2 : rc3;
    return VINF_SUCCESS;
}



#ifdef LOG_ENABLED

/** Logging helper. */
static size_t rtFsIsoVolGetStrippedLength(const char *pachField, size_t cchField)
{
    while (cchField > 0 && pachField[cchField - 1] == ' ')
        cchField--;
    return cchField;
}

/** Logging helper. */
static char *rtFsIsoVolGetMaybeUtf16Be(const char *pachField, size_t cchField, char *pszDst, size_t cbDst)
{
    /* Check the format by looking for zero bytes.  ISO-9660 doesn't allow zeros.
       This doesn't have to be a UTF-16BE string.  */
    size_t cFirstZeros  = 0;
    size_t cSecondZeros = 0;
    for (size_t off = 0; off + 1 < cchField; off += 2)
    {
        cFirstZeros  += pachField[off]     == '\0';
        cSecondZeros += pachField[off + 1] == '\0';
    }

    int    rc     = VINF_SUCCESS;
    char  *pszTmp = &pszDst[10];
    size_t cchRet = 0;
    if (cFirstZeros > cSecondZeros)
    {
        /* UTF-16BE / UTC-2BE: */
        if (cchField & 1)
        {
            if (pachField[cchField - 1] == '\0' || pachField[cchField - 1] == ' ')
                cchField--;
            else
                rc = VERR_INVALID_UTF16_ENCODING;
        }
        if (RT_SUCCESS(rc))
        {
            while (   cchField >= 2
                   && pachField[cchField - 1] == ' '
                   && pachField[cchField - 2] == '\0')
                cchField -= 2;

            rc = RTUtf16BigToUtf8Ex((PCRTUTF16)pachField, cchField / sizeof(RTUTF16), &pszTmp, cbDst - 10 - 1, &cchRet);
        }
        if (RT_SUCCESS(rc))
        {
            pszDst[0] = 'U';
            pszDst[1] = 'T';
            pszDst[2] = 'F';
            pszDst[3] = '-';
            pszDst[4] = '1';
            pszDst[5] = '6';
            pszDst[6] = 'B';
            pszDst[7] = 'E';
            pszDst[8] = ':';
            pszDst[9] = '\'';
            pszDst[10 + cchRet] = '\'';
            pszDst[10 + cchRet + 1] = '\0';
        }
        else
            RTStrPrintf(pszDst, cbDst, "UTF-16BE: %.*Rhxs", cchField, pachField);
    }
    else if (cSecondZeros > 0)
    {
        /* Little endian UTF-16 / UCS-2 (ASSUMES host is little endian, sorry) */
        if (cchField & 1)
        {
            if (pachField[cchField - 1] == '\0' || pachField[cchField - 1] == ' ')
                cchField--;
            else
                rc = VERR_INVALID_UTF16_ENCODING;
        }
        if (RT_SUCCESS(rc))
        {
            while (   cchField >= 2
                   && pachField[cchField - 1] == '\0'
                   && pachField[cchField - 2] == ' ')
                cchField -= 2;

            rc = RTUtf16ToUtf8Ex((PCRTUTF16)pachField, cchField / sizeof(RTUTF16), &pszTmp, cbDst - 10 - 1, &cchRet);
        }
        if (RT_SUCCESS(rc))
        {
            pszDst[0] = 'U';
            pszDst[1] = 'T';
            pszDst[2] = 'F';
            pszDst[3] = '-';
            pszDst[4] = '1';
            pszDst[5] = '6';
            pszDst[6] = 'L';
            pszDst[7] = 'E';
            pszDst[8] = ':';
            pszDst[9] = '\'';
            pszDst[10 + cchRet] = '\'';
            pszDst[10 + cchRet + 1] = '\0';
        }
        else
            RTStrPrintf(pszDst, cbDst, "UTF-16LE: %.*Rhxs", cchField, pachField);
    }
    else
    {
        /* ASSUME UTF-8/ASCII. */
        while (   cchField > 0
               && pachField[cchField - 1] == ' ')
            cchField--;
        rc = RTStrValidateEncodingEx(pachField, cchField, RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        if (RT_SUCCESS(rc))
            RTStrPrintf(pszDst, cbDst, "UTF-8: '%.*s'", cchField, pachField);
        else
            RTStrPrintf(pszDst, cbDst, "UNK-8: %.*Rhxs", cchField, pachField);
    }
    return pszDst;
}


/**
 * Logs the primary or supplementary volume descriptor
 *
 * @param   pVolDesc            The descriptor.
 */
static void rtFsIsoVolLogPrimarySupplementaryVolDesc(PCISO9660SUPVOLDESC pVolDesc)
{
    if (LogIs2Enabled())
    {
        char szTmp[384];
        Log2(("ISO9660:  fVolumeFlags:              %#RX8\n", pVolDesc->fVolumeFlags));
        Log2(("ISO9660:  achSystemId:               %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achSystemId, sizeof(pVolDesc->achSystemId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  achVolumeId:               %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achVolumeId, sizeof(pVolDesc->achVolumeId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  Unused73:                  {%#RX32,%#RX32}\n", RT_BE2H_U32(pVolDesc->Unused73.be), RT_LE2H_U32(pVolDesc->Unused73.le)));
        Log2(("ISO9660:  VolumeSpaceSize:           {%#RX32,%#RX32}\n", RT_BE2H_U32(pVolDesc->VolumeSpaceSize.be), RT_LE2H_U32(pVolDesc->VolumeSpaceSize.le)));
        Log2(("ISO9660:  abEscapeSequences:         '%.*s'\n", rtFsIsoVolGetStrippedLength((char *)pVolDesc->abEscapeSequences, sizeof(pVolDesc->abEscapeSequences)), pVolDesc->abEscapeSequences));
        Log2(("ISO9660:  cVolumesInSet:             {%#RX16,%#RX16}\n", RT_BE2H_U16(pVolDesc->cVolumesInSet.be), RT_LE2H_U16(pVolDesc->cVolumesInSet.le)));
        Log2(("ISO9660:  VolumeSeqNo:               {%#RX16,%#RX16}\n", RT_BE2H_U16(pVolDesc->VolumeSeqNo.be), RT_LE2H_U16(pVolDesc->VolumeSeqNo.le)));
        Log2(("ISO9660:  cbLogicalBlock:            {%#RX16,%#RX16}\n", RT_BE2H_U16(pVolDesc->cbLogicalBlock.be), RT_LE2H_U16(pVolDesc->cbLogicalBlock.le)));
        Log2(("ISO9660:  cbPathTable:               {%#RX32,%#RX32}\n", RT_BE2H_U32(pVolDesc->cbPathTable.be), RT_LE2H_U32(pVolDesc->cbPathTable.le)));
        Log2(("ISO9660:  offTypeLPathTable:         %#RX32\n", RT_LE2H_U32(pVolDesc->offTypeLPathTable)));
        Log2(("ISO9660:  offOptionalTypeLPathTable: %#RX32\n", RT_LE2H_U32(pVolDesc->offOptionalTypeLPathTable)));
        Log2(("ISO9660:  offTypeMPathTable:         %#RX32\n", RT_BE2H_U32(pVolDesc->offTypeMPathTable)));
        Log2(("ISO9660:  offOptionalTypeMPathTable: %#RX32\n", RT_BE2H_U32(pVolDesc->offOptionalTypeMPathTable)));
        Log2(("ISO9660:  achVolumeSetId:            %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achVolumeSetId, sizeof(pVolDesc->achVolumeSetId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  achPublisherId:            %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achPublisherId, sizeof(pVolDesc->achPublisherId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  achDataPreparerId:         %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achDataPreparerId, sizeof(pVolDesc->achDataPreparerId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  achApplicationId:          %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achApplicationId, sizeof(pVolDesc->achApplicationId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  achCopyrightFileId:        %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achCopyrightFileId, sizeof(pVolDesc->achCopyrightFileId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  achAbstractFileId:         %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achAbstractFileId, sizeof(pVolDesc->achAbstractFileId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  achBibliographicFileId:    %s\n", rtFsIsoVolGetMaybeUtf16Be(pVolDesc->achBibliographicFileId, sizeof(pVolDesc->achBibliographicFileId), szTmp, sizeof(szTmp)) ));
        Log2(("ISO9660:  BirthTime:                 %.4s-%.2s-%.2s %.2s:%.2s:%.2s.%.2s%+03d\n",
              pVolDesc->BirthTime.achYear,
              pVolDesc->BirthTime.achMonth,
              pVolDesc->BirthTime.achDay,
              pVolDesc->BirthTime.achHour,
              pVolDesc->BirthTime.achMinute,
              pVolDesc->BirthTime.achSecond,
              pVolDesc->BirthTime.achCentisecond,
              pVolDesc->BirthTime.offUtc*4/60));
        Log2(("ISO9660:  ModifyTime:                %.4s-%.2s-%.2s %.2s:%.2s:%.2s.%.2s%+03d\n",
              pVolDesc->ModifyTime.achYear,
              pVolDesc->ModifyTime.achMonth,
              pVolDesc->ModifyTime.achDay,
              pVolDesc->ModifyTime.achHour,
              pVolDesc->ModifyTime.achMinute,
              pVolDesc->ModifyTime.achSecond,
              pVolDesc->ModifyTime.achCentisecond,
              pVolDesc->ModifyTime.offUtc*4/60));
        Log2(("ISO9660:  ExpireTime:                %.4s-%.2s-%.2s %.2s:%.2s:%.2s.%.2s%+03d\n",
              pVolDesc->ExpireTime.achYear,
              pVolDesc->ExpireTime.achMonth,
              pVolDesc->ExpireTime.achDay,
              pVolDesc->ExpireTime.achHour,
              pVolDesc->ExpireTime.achMinute,
              pVolDesc->ExpireTime.achSecond,
              pVolDesc->ExpireTime.achCentisecond,
              pVolDesc->ExpireTime.offUtc*4/60));
        Log2(("ISO9660:  EffectiveTime:             %.4s-%.2s-%.2s %.2s:%.2s:%.2s.%.2s%+03d\n",
              pVolDesc->EffectiveTime.achYear,
              pVolDesc->EffectiveTime.achMonth,
              pVolDesc->EffectiveTime.achDay,
              pVolDesc->EffectiveTime.achHour,
              pVolDesc->EffectiveTime.achMinute,
              pVolDesc->EffectiveTime.achSecond,
              pVolDesc->EffectiveTime.achCentisecond,
              pVolDesc->EffectiveTime.offUtc*4/60));
        Log2(("ISO9660:  bFileStructureVersion:     %#RX8\n", pVolDesc->bFileStructureVersion));
        Log2(("ISO9660:  bReserved883:              %#RX8\n", pVolDesc->bReserved883));

        Log2(("ISO9660:  RootDir.cbDirRec:                   %#RX8\n", pVolDesc->RootDir.DirRec.cbDirRec));
        Log2(("ISO9660:  RootDir.cExtAttrBlocks:             %#RX8\n", pVolDesc->RootDir.DirRec.cExtAttrBlocks));
        Log2(("ISO9660:  RootDir.offExtent:                  {%#RX32,%#RX32}\n", RT_BE2H_U32(pVolDesc->RootDir.DirRec.offExtent.be), RT_LE2H_U32(pVolDesc->RootDir.DirRec.offExtent.le)));
        Log2(("ISO9660:  RootDir.cbData:                     {%#RX32,%#RX32}\n", RT_BE2H_U32(pVolDesc->RootDir.DirRec.cbData.be), RT_LE2H_U32(pVolDesc->RootDir.DirRec.cbData.le)));
        Log2(("ISO9660:  RootDir.RecTime:                    %04u-%02u-%02u %02u:%02u:%02u%+03d\n",
              pVolDesc->RootDir.DirRec.RecTime.bYear + 1900,
              pVolDesc->RootDir.DirRec.RecTime.bMonth,
              pVolDesc->RootDir.DirRec.RecTime.bDay,
              pVolDesc->RootDir.DirRec.RecTime.bHour,
              pVolDesc->RootDir.DirRec.RecTime.bMinute,
              pVolDesc->RootDir.DirRec.RecTime.bSecond,
              pVolDesc->RootDir.DirRec.RecTime.offUtc*4/60));
        Log2(("ISO9660:  RootDir.RecTime.fFileFlags:         %RX8\n", pVolDesc->RootDir.DirRec.fFileFlags));
        Log2(("ISO9660:  RootDir.RecTime.bFileUnitSize:      %RX8\n", pVolDesc->RootDir.DirRec.bFileUnitSize));
        Log2(("ISO9660:  RootDir.RecTime.bInterleaveGapSize: %RX8\n", pVolDesc->RootDir.DirRec.bInterleaveGapSize));
        Log2(("ISO9660:  RootDir.RecTime.VolumeSeqNo:        {%#RX16,%#RX16}\n", RT_BE2H_U16(pVolDesc->RootDir.DirRec.VolumeSeqNo.be), RT_LE2H_U16(pVolDesc->RootDir.DirRec.VolumeSeqNo.le)));
        Log2(("ISO9660:  RootDir.RecTime.bFileIdLength:      %RX8\n", pVolDesc->RootDir.DirRec.bFileIdLength));
        Log2(("ISO9660:  RootDir.RecTime.achFileId:          '%.*s'\n", pVolDesc->RootDir.DirRec.bFileIdLength, pVolDesc->RootDir.DirRec.achFileId));
        uint32_t offSysUse = RT_UOFFSETOF_DYN(ISO9660DIRREC, achFileId[pVolDesc->RootDir.DirRec.bFileIdLength])
                           + !(pVolDesc->RootDir.DirRec.bFileIdLength & 1);
        if (offSysUse < pVolDesc->RootDir.DirRec.cbDirRec)
        {
            Log2(("ISO9660:  RootDir System Use:\n%.*RhxD\n",
                  pVolDesc->RootDir.DirRec.cbDirRec - offSysUse, &pVolDesc->RootDir.ab[offSysUse]));
        }
    }
}

#endif /* LOG_ENABLED */

/**
 * Deal with a root directory from a primary or supplemental descriptor.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO 9660 instance being initialized.
 * @param   pRootDir        The root directory record to check out.
 * @param   pDstRootDir     Where to store a copy of the root dir record.
 * @param   pErrInfo        Where to return additional error info.  Can be NULL.
 */
static int rtFsIsoVolHandleRootDir(PRTFSISOVOL pThis, PCISO9660DIRREC pRootDir,
                                   PISO9660DIRREC pDstRootDir, PRTERRINFO pErrInfo)
{
    if (pRootDir->cbDirRec < RT_UOFFSETOF(ISO9660DIRREC, achFileId))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Root dir record size is too small: %#x (min %#x)",
                                   pRootDir->cbDirRec, RT_UOFFSETOF(ISO9660DIRREC, achFileId));

    if (!(pRootDir->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                   "Root dir is not flagged as directory: %#x", pRootDir->fFileFlags);
    if (pRootDir->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                   "Root dir is cannot be multi-extent: %#x", pRootDir->fFileFlags);

    if (RT_LE2H_U32(pRootDir->cbData.le) != RT_BE2H_U32(pRootDir->cbData.be))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Invalid root dir size: {%#RX32,%#RX32}",
                                   RT_BE2H_U32(pRootDir->cbData.be), RT_LE2H_U32(pRootDir->cbData.le));
    if (RT_LE2H_U32(pRootDir->cbData.le) == 0)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Zero sized root dir");

    if (RT_LE2H_U32(pRootDir->offExtent.le) != RT_BE2H_U32(pRootDir->offExtent.be))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Invalid root dir extent: {%#RX32,%#RX32}",
                                   RT_BE2H_U32(pRootDir->offExtent.be), RT_LE2H_U32(pRootDir->offExtent.le));

    if (RT_LE2H_U16(pRootDir->VolumeSeqNo.le) != RT_BE2H_U16(pRootDir->VolumeSeqNo.be))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Invalid root dir volume sequence ID: {%#RX16,%#RX16}",
                                   RT_BE2H_U16(pRootDir->VolumeSeqNo.be), RT_LE2H_U16(pRootDir->VolumeSeqNo.le));
    if (RT_LE2H_U16(pRootDir->VolumeSeqNo.le) != pThis->idPrimaryVol)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Expected root dir to have same volume sequence number as primary volume: %#x, expected %#x",
                                   RT_LE2H_U16(pRootDir->VolumeSeqNo.le), pThis->idPrimaryVol);

    /*
     * Seems okay, copy it.
     */
    *pDstRootDir = *pRootDir;
    return VINF_SUCCESS;
}


/**
 * Deal with a primary volume descriptor.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO 9660 instance being initialized.
 * @param   pVolDesc        The volume descriptor to handle.
 * @param   offVolDesc      The disk offset of the volume descriptor.
 * @param   pRootDir        Where to return a copy of the root directory record.
 * @param   poffRootDirRec  Where to return the disk offset of the root dir.
 * @param   pErrInfo        Where to return additional error info.  Can be NULL.
 */
static int rtFsIsoVolHandlePrimaryVolDesc(PRTFSISOVOL pThis, PCISO9660PRIMARYVOLDESC pVolDesc, uint32_t offVolDesc,
                                          PISO9660DIRREC pRootDir, uint64_t *poffRootDirRec, PRTERRINFO pErrInfo)
{
    if (pVolDesc->bFileStructureVersion != ISO9660_FILE_STRUCTURE_VERSION)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Unsupported file structure version: %#x", pVolDesc->bFileStructureVersion);

    /*
     * Take down the location of the primary volume descriptor so we can get
     * the volume lable and other info from it later.
     */
    pThis->offPrimaryVolDesc = offVolDesc;

    /*
     * We need the block size ...
     */
    pThis->cbBlock = RT_LE2H_U16(pVolDesc->cbLogicalBlock.le);
    if (   pThis->cbBlock != RT_BE2H_U16(pVolDesc->cbLogicalBlock.be)
        || !RT_IS_POWER_OF_TWO(pThis->cbBlock)
        || pThis->cbBlock / pThis->cbSector < 1)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Invalid logical block size: {%#RX16,%#RX16}",
                                   RT_BE2H_U16(pVolDesc->cbLogicalBlock.be), RT_LE2H_U16(pVolDesc->cbLogicalBlock.le));
    if (pThis->cbBlock / pThis->cbSector > 128)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Unsupported block size: %#x\n", pThis->cbBlock);

    /*
     * ... volume space size ...
     */
    pThis->cBlocksInPrimaryVolumeSpace = RT_LE2H_U32(pVolDesc->VolumeSpaceSize.le);
    if (pThis->cBlocksInPrimaryVolumeSpace != RT_BE2H_U32(pVolDesc->VolumeSpaceSize.be))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Invalid volume space size: {%#RX32,%#RX32}",
                                   RT_BE2H_U32(pVolDesc->VolumeSpaceSize.be), RT_LE2H_U32(pVolDesc->VolumeSpaceSize.le));
    pThis->cbPrimaryVolumeSpace = pThis->cBlocksInPrimaryVolumeSpace * (uint64_t)pThis->cbBlock;

    /*
     * ... number of volumes in the set ...
     */
    pThis->cVolumesInSet = RT_LE2H_U16(pVolDesc->cVolumesInSet.le);
    if (   pThis->cVolumesInSet != RT_BE2H_U16(pVolDesc->cVolumesInSet.be)
        || pThis->cVolumesInSet == 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Invalid volume set size: {%#RX16,%#RX16}",
                                   RT_BE2H_U16(pVolDesc->cVolumesInSet.be), RT_LE2H_U16(pVolDesc->cVolumesInSet.le));
    if (pThis->cVolumesInSet > 32)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Too large volume set size: %#x\n", pThis->cVolumesInSet);

    /*
     * ... primary volume sequence ID ...
     */
    pThis->idPrimaryVol = RT_LE2H_U16(pVolDesc->VolumeSeqNo.le);
    if (pThis->idPrimaryVol != RT_BE2H_U16(pVolDesc->VolumeSeqNo.be))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Invalid volume sequence ID: {%#RX16,%#RX16}",
                                   RT_BE2H_U16(pVolDesc->VolumeSeqNo.be), RT_LE2H_U16(pVolDesc->VolumeSeqNo.le));
    if (   pThis->idPrimaryVol > pThis->cVolumesInSet
        || pThis->idPrimaryVol < 1)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Volume sequence ID out of of bound: %#x (1..%#x)\n", pThis->idPrimaryVol, pThis->cVolumesInSet);

    /*
     * ... and the root directory record.
     */
    *poffRootDirRec = offVolDesc + RT_UOFFSETOF(ISO9660PRIMARYVOLDESC, RootDir.DirRec);
    return rtFsIsoVolHandleRootDir(pThis, &pVolDesc->RootDir.DirRec, pRootDir, pErrInfo);
}


/**
 * Deal with a supplementary volume descriptor.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO 9660 instance being initialized.
 * @param   pVolDesc        The volume descriptor to handle.
 * @param   offVolDesc      The disk offset of the volume descriptor.
 * @param   pbUcs2Level     Where to return the joliet level, if found. Caller
 *                          initializes this to zero, we'll return 1, 2 or 3 if
 *                          joliet was detected.
 * @param   pRootDir        Where to return the root directory, if found.
 * @param   poffRootDirRec  Where to return the disk offset of the root dir.
 * @param   pErrInfo        Where to return additional error info.  Can be NULL.
 */
static int rtFsIsoVolHandleSupplementaryVolDesc(PRTFSISOVOL pThis, PCISO9660SUPVOLDESC pVolDesc, uint32_t offVolDesc,
                                                uint8_t *pbUcs2Level, PISO9660DIRREC pRootDir, uint64_t *poffRootDirRec,
                                                PRTERRINFO pErrInfo)
{
    if (pVolDesc->bFileStructureVersion != ISO9660_FILE_STRUCTURE_VERSION)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Unsupported file structure version: %#x", pVolDesc->bFileStructureVersion);

    /*
     * Is this a joliet volume descriptor?  If not, we probably don't need to
     * care about it.
     */
    if (   pVolDesc->abEscapeSequences[0] != ISO9660_JOLIET_ESC_SEQ_0
        || pVolDesc->abEscapeSequences[1] != ISO9660_JOLIET_ESC_SEQ_1
        || (   pVolDesc->abEscapeSequences[2] != ISO9660_JOLIET_ESC_SEQ_2_LEVEL_1
            && pVolDesc->abEscapeSequences[2] != ISO9660_JOLIET_ESC_SEQ_2_LEVEL_2
            && pVolDesc->abEscapeSequences[2] != ISO9660_JOLIET_ESC_SEQ_2_LEVEL_3))
        return VINF_SUCCESS;

    /*
     * Skip if joliet is unwanted.
     */
    if (pThis->fFlags & RTFSISO9660_F_NO_JOLIET)
        return VINF_SUCCESS;

    /*
     * Check that the joliet descriptor matches the primary one.
     * Note! These are our assumptions and may be wrong.
     */
    if (pThis->cbBlock == 0)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                 "Supplementary joliet volume descriptor is not supported when appearing before the primary volume descriptor");
    if (ISO9660_GET_ENDIAN(&pVolDesc->cbLogicalBlock) != pThis->cbBlock)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Logical block size for joliet volume descriptor differs from primary: %#RX16 vs %#RX16\n",
                                   ISO9660_GET_ENDIAN(&pVolDesc->cbLogicalBlock), pThis->cbBlock);
#if 0 /* Not necessary. */
    /* Used to be !=, changed to > for ubuntu 20.10 and later.  Wonder if they exclude a few files
       and thus end up with a different total.  Obviously, this test is a big bogus, as we don't
       really seem to care about the value at all... */
    if (ISO9660_GET_ENDIAN(&pVolDesc->VolumeSpaceSize) > pThis->cBlocksInPrimaryVolumeSpace)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Volume space size for joliet volume descriptor differs from primary: %#RX32 vs %#RX32\n",
                                   ISO9660_GET_ENDIAN(&pVolDesc->VolumeSpaceSize), pThis->cBlocksInPrimaryVolumeSpace);
#endif
    if (ISO9660_GET_ENDIAN(&pVolDesc->cVolumesInSet) != pThis->cVolumesInSet)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Volume set size for joliet volume descriptor differs from primary: %#RX16 vs %#RX32\n",
                                   ISO9660_GET_ENDIAN(&pVolDesc->cVolumesInSet), pThis->cVolumesInSet);
    if (ISO9660_GET_ENDIAN(&pVolDesc->VolumeSeqNo) != pThis->idPrimaryVol)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Volume sequence ID for joliet volume descriptor differs from primary: %#RX16 vs %#RX32\n",
                                   ISO9660_GET_ENDIAN(&pVolDesc->VolumeSeqNo), pThis->idPrimaryVol);

    if (*pbUcs2Level != 0)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "More than one supplementary joliet volume descriptor");

    /*
     * Switch to the joliet root dir as it has UTF-16 stuff in it.
     */
    int rc = rtFsIsoVolHandleRootDir(pThis, &pVolDesc->RootDir.DirRec, pRootDir, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        *poffRootDirRec = offVolDesc + RT_UOFFSETOF(ISO9660SUPVOLDESC, RootDir.DirRec);
        *pbUcs2Level    = pVolDesc->abEscapeSequences[2] == ISO9660_JOLIET_ESC_SEQ_2_LEVEL_1 ? 1
                        : pVolDesc->abEscapeSequences[2] == ISO9660_JOLIET_ESC_SEQ_2_LEVEL_2 ? 2 : 3;
        Log(("ISO9660: Joliet with UCS-2 level %u\n", *pbUcs2Level));

        /*
         * Take down the location of the secondary volume descriptor so we can get
         * the volume lable and other info from it later.
         */
        pThis->offSecondaryVolDesc = offVolDesc;
    }
    return rc;
}



/**
 * Worker for RTFsIso9660VolOpen.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO VFS instance to initialize.
 * @param   hVfsSelf        The ISO VFS handle (no reference consumed).
 * @param   hVfsBacking     The file backing the alleged ISO file system.
 *                          Reference is consumed (via rtFsIsoVol_Close).
 * @param   fFlags          Flags, RTFSISO9660_F_XXX.
 * @param   pErrInfo        Where to return additional error info.  Can be NULL.
 */
static int rtFsIsoVolTryInit(PRTFSISOVOL pThis, RTVFS hVfsSelf, RTVFSFILE hVfsBacking, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    uint32_t const cbSector = 2048;

    /*
     * First initialize the state so that rtFsIsoVol_Close won't trip up.
     */
    pThis->hVfsSelf                     = hVfsSelf;
    pThis->hVfsBacking                  = hVfsBacking; /* Caller referenced it for us, we consume it; rtFsIsoVol_Close releases it. */
    pThis->cbBacking                    = 0;
    pThis->cBackingSectors              = 0;
    pThis->fFlags                       = fFlags;
    pThis->cbSector                     = cbSector;
    pThis->cbBlock                      = 0;
    pThis->cBlocksInPrimaryVolumeSpace  = 0;
    pThis->cbPrimaryVolumeSpace         = 0;
    pThis->cVolumesInSet                = 0;
    pThis->idPrimaryVol                 = UINT32_MAX;
    pThis->fIsUtf16                     = false;
    pThis->pRootDir                     = NULL;
    pThis->fHaveRock                    = false;
    pThis->offSuspSkip                  = 0;
    pThis->offRockBuf                   = UINT64_MAX;

    /*
     * Do init stuff that may fail.
     */
    int rc = RTCritSectInit(&pThis->RockBufLock);
    AssertRCReturn(rc, rc);

    rc = RTVfsFileQuerySize(hVfsBacking, &pThis->cbBacking);
    if (RT_SUCCESS(rc))
        pThis->cBackingSectors = pThis->cbBacking / pThis->cbSector;
    else
        return rc;

    /*
     * Read the volume descriptors starting at logical sector 16.
     */
    union
    {
        uint8_t                 ab[RTFSISO_MAX_LOGICAL_BLOCK_SIZE];
        uint16_t                au16[RTFSISO_MAX_LOGICAL_BLOCK_SIZE / 2];
        uint32_t                au32[RTFSISO_MAX_LOGICAL_BLOCK_SIZE / 4];
        ISO9660VOLDESCHDR       VolDescHdr;
        ISO9660BOOTRECORD       BootRecord;
        ISO9660PRIMARYVOLDESC   PrimaryVolDesc;
        ISO9660SUPVOLDESC       SupVolDesc;
        ISO9660VOLPARTDESC      VolPartDesc;
    } Buf;
    RT_ZERO(Buf);

    uint64_t        offRootDirRec           = UINT64_MAX;
    ISO9660DIRREC   RootDir;
    RT_ZERO(RootDir);

    uint64_t        offJolietRootDirRec     = UINT64_MAX;
    uint8_t         bJolietUcs2Level        = 0;
    ISO9660DIRREC   JolietRootDir;
    RT_ZERO(JolietRootDir);

    uint8_t         uUdfLevel               = 0;
    uint64_t        offUdfBootVolDesc       = UINT64_MAX;

    uint32_t        cPrimaryVolDescs        = 0;
    uint32_t        cSupplementaryVolDescs  = 0;
    uint32_t        cBootRecordVolDescs     = 0;
    uint32_t        offVolDesc              = 16 * cbSector;
    enum
    {
        kStateStart = 0,
        kStateNoSeq,
        kStateCdSeq,
        kStateUdfSeq
    }               enmState = kStateStart;
    for (uint32_t iVolDesc = 0; ; iVolDesc++, offVolDesc += cbSector)
    {
        if (iVolDesc > 32)
            return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "More than 32 volume descriptors, doesn't seem right...");

        /* Read the next one and check the signature. */
        rc = RTVfsFileReadAt(hVfsBacking, offVolDesc, &Buf, cbSector, NULL);
        if (RT_FAILURE(rc))
            return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Unable to read volume descriptor #%u", iVolDesc);

#define MATCH_STD_ID(a_achStdId1, a_szStdId2) \
            (   (a_achStdId1)[0] == (a_szStdId2)[0] \
             && (a_achStdId1)[1] == (a_szStdId2)[1] \
             && (a_achStdId1)[2] == (a_szStdId2)[2] \
             && (a_achStdId1)[3] == (a_szStdId2)[3] \
             && (a_achStdId1)[4] == (a_szStdId2)[4] )
#define MATCH_HDR(a_pStd, a_bType2, a_szStdId2, a_bVer2) \
            (    MATCH_STD_ID((a_pStd)->achStdId, a_szStdId2) \
             && (a_pStd)->bDescType    == (a_bType2) \
             && (a_pStd)->bDescVersion == (a_bVer2) )

        /*
         * ISO 9660 ("CD001").
         */
        if (   (   enmState == kStateStart
                || enmState == kStateCdSeq
                || enmState == kStateNoSeq)
            && MATCH_STD_ID(Buf.VolDescHdr.achStdId, ISO9660VOLDESC_STD_ID) )
        {
            enmState = kStateCdSeq;

            /* Do type specific handling. */
            Log(("ISO9660: volume desc #%u: type=%#x\n", iVolDesc, Buf.VolDescHdr.bDescType));
            if (Buf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_PRIMARY)
            {
                cPrimaryVolDescs++;
                if (Buf.VolDescHdr.bDescVersion != ISO9660PRIMARYVOLDESC_VERSION)
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                               "Unsupported primary volume descriptor version: %#x", Buf.VolDescHdr.bDescVersion);
#ifdef LOG_ENABLED
                rtFsIsoVolLogPrimarySupplementaryVolDesc(&Buf.SupVolDesc);
#endif
                if (cPrimaryVolDescs == 1)
                    rc = rtFsIsoVolHandlePrimaryVolDesc(pThis, &Buf.PrimaryVolDesc, offVolDesc, &RootDir, &offRootDirRec, pErrInfo);
                else if (cPrimaryVolDescs == 2)
                    Log(("ISO9660: ignoring 2nd primary descriptor\n")); /* so we can read the w2k3 ifs kit */
                else
                    return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "More than one primary volume descriptor");
            }
            else if (Buf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_SUPPLEMENTARY)
            {
                cSupplementaryVolDescs++;
                if (Buf.VolDescHdr.bDescVersion != ISO9660SUPVOLDESC_VERSION)
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                               "Unsupported supplemental volume descriptor version: %#x", Buf.VolDescHdr.bDescVersion);
#ifdef LOG_ENABLED
                rtFsIsoVolLogPrimarySupplementaryVolDesc(&Buf.SupVolDesc);
#endif
                rc = rtFsIsoVolHandleSupplementaryVolDesc(pThis, &Buf.SupVolDesc, offVolDesc, &bJolietUcs2Level, &JolietRootDir,
                                                          &offJolietRootDirRec, pErrInfo);
            }
            else if (Buf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_BOOT_RECORD)
            {
                cBootRecordVolDescs++;
            }
            else if (Buf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_TERMINATOR)
            {
                if (!cPrimaryVolDescs)
                    return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "No primary volume descriptor");
                enmState = kStateNoSeq;
            }
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                           "Unknown volume descriptor: %#x", Buf.VolDescHdr.bDescType);
        }
        /*
         * UDF volume recognition sequence (VRS).
         */
        else if (   (   enmState == kStateNoSeq
                     || enmState == kStateStart)
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_BEGIN, UDF_EXT_VOL_DESC_VERSION) )
        {
            Log(("ISO/UDF: volume desc #%u: BEA01\n", iVolDesc));
            if (uUdfLevel == 0)
                enmState = kStateUdfSeq;
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Only one BEA01 sequence is supported");
        }
        else if (   enmState == kStateUdfSeq
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_NSR_02, UDF_EXT_VOL_DESC_VERSION) )
        {
            Log(("ISO/UDF: volume desc #%u: NSR02\n", iVolDesc));
            uUdfLevel = 2;
        }
        else if (   enmState == kStateUdfSeq
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_NSR_03, UDF_EXT_VOL_DESC_VERSION) )
        {
            Log(("ISO/UDF: volume desc #%u: NSR03\n", iVolDesc));
            uUdfLevel = 3;
        }
        else if (   enmState == kStateUdfSeq
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_BOOT, UDF_EXT_VOL_DESC_VERSION) )
        {
            Log(("ISO/UDF: volume desc #%u: BOOT2\n", iVolDesc));
            if (offUdfBootVolDesc == UINT64_MAX)
                offUdfBootVolDesc = iVolDesc * cbSector;
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Only one BOOT2 descriptor is supported");
        }
        else if (   enmState == kStateUdfSeq
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_TERM, UDF_EXT_VOL_DESC_VERSION) )
        {
            Log(("ISO/UDF: volume desc #%u: TEA01\n", iVolDesc));
            if (uUdfLevel != 0)
                enmState = kStateNoSeq;
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Found BEA01 & TEA01, but no NSR02 or NSR03 descriptors");
        }
        /*
         * Unknown, probably the end.
         */
        else if (enmState == kStateNoSeq)
        {
            Log(("IS9660: volume desc #%u: bDescType=%#x achStdId=%.5Rhxs bDescVers=%#x - stop\n",
                 iVolDesc, Buf.VolDescHdr.bDescType, &Buf.VolDescHdr.achStdId[0], Buf.VolDescHdr.bDescVersion));
            break;
        }
        else if (enmState == kStateStart)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT,
                                       "Not ISO? Unable to recognize volume descriptor signature: %.5Rhxs", Buf.VolDescHdr.achStdId);
        else if (enmState == kStateCdSeq)
        {
#if 1
            /* The warp server for ebusiness update ISOs known as ACP2 & MCP2 ends up here,
               as they do in deed miss a terminator volume descriptor and we're now at the
               root directory already. Just detect this, ignore it and get on with things. */
            Log(("rtFsIsoVolTryInit: Ignoring missing ISO 9660 terminator volume descriptor (found %.5Rhxs).\n",
                 Buf.VolDescHdr.achStdId));
            break;
#else
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "Missing ISO 9660 terminator volume descriptor? (Found %.5Rhxs)", Buf.VolDescHdr.achStdId);
#endif
        }
        else if (enmState == kStateUdfSeq)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "Missing UDF terminator volume descriptor? (Found %.5Rhxs)", Buf.VolDescHdr.achStdId);
        else
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT,
                                       "Unknown volume descriptor signature found at sector %u: %.5Rhxs",
                                       16 + iVolDesc, Buf.VolDescHdr.achStdId);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * If we found a UDF VRS and are interested in UDF, we have more work to do here.
     */
    if (uUdfLevel > 0 && !(fFlags & RTFSISO9660_F_NO_UDF))
    {
        Log(("rtFsIsoVolTryInit: uUdfLevel=%d\n", uUdfLevel));
        rc = rtFsIsoVolHandleUdfDetection(pThis, &uUdfLevel, offUdfBootVolDesc, Buf.ab, sizeof(Buf), pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
        AssertReturn(uUdfLevel > 0 || !(fFlags & RTFSISO9660_F_ONLY_UDF), VERR_INTERNAL_ERROR_3);
    }

    /*
     * Decide which to prefer.
     *
     * By default we pick UDF over any of the two ISO 9960, there is currently
     * no way to override this without using the RTFSISO9660_F_NO_XXX options.
     *
     * If there isn't UDF, we may be faced with choosing between joliet and
     * rock ridge.  The joliet option is generally favorable as we don't have
     * to guess wrt to the file name encoding.  So, we'll pick that for now.
     *
     * Note! Should we change this preference for joliet, there fun wrt making sure
     *       there really is rock ridge stuff in the primary volume as well as
     *       making sure there really is anything of value in the primary volume.
     */
    if (uUdfLevel > 0 && !(fFlags & RTFSISO9660_F_NO_UDF))
    {
        pThis->enmType = RTFSISOVOLTYPE_UDF;
        rc = rtFsIsoDirShrd_NewUdf(pThis, NULL /*pParent*/, &pThis->Udf.VolInfo.RootDirIcb,
                                   NULL /*pFileIdDesc*/, 0 /*offInDir*/, &pThis->pRootDir);
        /** @todo fall back on failure? */
        return rc;
    }

    if (fFlags & RTFSISO9660_F_ONLY_UDF)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "No UDF found (only UDF flag set)");

    if (bJolietUcs2Level != 0)
    {
        pThis->enmType = RTFSISOVOLTYPE_JOLIET;
        pThis->fIsUtf16 = true;
        return rtFsIsoDirShrd_New9660(pThis, NULL, &JolietRootDir, 1, offJolietRootDirRec, NULL, &pThis->pRootDir);
    }
    pThis->enmType = RTFSISOVOLTYPE_ISO9960;
    return rtFsIsoDirShrd_New9660(pThis, NULL, &RootDir, 1, offRootDirRec, NULL, &pThis->pRootDir);
}


/**
 * Opens an ISO 9660 file system volume.
 *
 * @returns IPRT status code.
 * @param   hVfsFileIn      The file or device backing the volume.
 * @param   fFlags          RTFSISO9660_F_XXX.
 * @param   phVfs           Where to return the virtual file system handle.
 * @param   pErrInfo        Where to return additional error information.
 */
RTDECL(int) RTFsIso9660VolOpen(RTVFSFILE hVfsFileIn, uint32_t fFlags, PRTVFS phVfs, PRTERRINFO pErrInfo)
{
    /*
     * Quick input validation.
     */
    AssertPtrReturn(phVfs, VERR_INVALID_POINTER);
    *phVfs = NIL_RTVFS;
    AssertReturn(!(fFlags & ~RTFSISO9660_F_VALID_MASK), VERR_INVALID_FLAGS);
    if (fFlags & RTFSISO9660_F_ONLY_UDF)
        fFlags &= ~RTFSISO9660_F_NO_UDF;

    uint32_t cRefs = RTVfsFileRetain(hVfsFileIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create a new ISO VFS instance and try initialize it using the given input file.
     */
    RTVFS       hVfs   = NIL_RTVFS;
    PRTFSISOVOL pThis = NULL;
    int rc = RTVfsNew(&g_rtFsIsoVolOps, sizeof(RTFSISOVOL), NIL_RTVFS, RTVFSLOCK_CREATE_RW, &hVfs, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        rc = rtFsIsoVolTryInit(pThis, hVfs, hVfsFileIn, fFlags, pErrInfo);
        if (RT_SUCCESS(rc))
            *phVfs = hVfs;
        else
            RTVfsRelease(hVfs);
    }
    else
        RTVfsFileRelease(hVfsFileIn);
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainIsoFsVol_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                                     PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec);

    /*
     * Basic checks.
     */
    if (pElement->enmTypeIn != RTVFSOBJTYPE_FILE)
        return pElement->enmTypeIn == RTVFSOBJTYPE_INVALID ? VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT : VERR_VFS_CHAIN_TAKES_FILE;
    if (   pElement->enmType != RTVFSOBJTYPE_VFS
        && pElement->enmType != RTVFSOBJTYPE_DIR)
        return VERR_VFS_CHAIN_ONLY_DIR_OR_VFS;
    if (pElement->cArgs > 1)
        return VERR_VFS_CHAIN_AT_MOST_ONE_ARG;

    /*
     * Parse the flag if present, save in pElement->uProvider.
     */
    uint32_t fFlags = 0;
    if (pElement->cArgs > 0)
    {
        for (uint32_t iArg = 0; iArg < pElement->cArgs; iArg++)
        {
            const char *psz = pElement->paArgs[iArg].psz;
            if (*psz)
            {
                if (!strcmp(psz, "nojoliet"))
                    fFlags |= RTFSISO9660_F_NO_JOLIET;
                else if (!strcmp(psz, "norock"))
                    fFlags |= RTFSISO9660_F_NO_ROCK;
                else if (!strcmp(psz, "noudf"))
                    fFlags |= RTFSISO9660_F_NO_UDF;
                else if (!strcmp(psz, "onlyudf") || strcmp(psz, "only-udf"))
                    fFlags = (fFlags & ~RTFSISO9660_F_NO_UDF) | RTFSISO9660_F_ONLY_UDF;
                else
                {
                    *poffError = pElement->paArgs[iArg].offSpec;
                    return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Only knows: 'nojoliet' and 'norock'");
                }
            }
        }
    }

    pElement->uProvider = fFlags;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtVfsChainIsoFsVol_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                        PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                        PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, poffError);

    int         rc;
    RTVFSFILE   hVfsFileIn = RTVfsObjToFile(hPrevVfsObj);
    if (hVfsFileIn != NIL_RTVFSFILE)
    {
        RTVFS hVfs;
        rc = RTFsIso9660VolOpen(hVfsFileIn, pElement->uProvider, &hVfs, pErrInfo);
        RTVfsFileRelease(hVfsFileIn);
        if (RT_SUCCESS(rc))
        {
            *phVfsObj = RTVfsObjFromVfs(hVfs);
            RTVfsRelease(hVfs);
            if (*phVfsObj != NIL_RTVFSOBJ)
                return VINF_SUCCESS;
            rc = VERR_VFS_CHAIN_CAST_FAILED;
        }
    }
    else
        rc = VERR_VFS_CHAIN_CAST_FAILED;
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtVfsChainIsoFsVol_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                             PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                             PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pReuseSpec);
    if (   pElement->paArgs[0].uProvider == pReuseElement->paArgs[0].uProvider
        || !pReuseElement->paArgs[0].uProvider)
        return true;
    return false;
}


/** VFS chain element 'file'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainIsoFsVolReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "isofs",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Open a ISO 9660 or UDF file system, requires a file object on the left side.\n"
                                "The 'noudf' option make it ignore any UDF.\n"
                                "The 'nojoliet' option make it ignore any joliet supplemental volume.\n"
                                "The 'norock' option make it ignore any rock ridge info.\n"
                                "The 'onlyudf' option make it use UDF or fail.\n",
    /* pfnValidate = */         rtVfsChainIsoFsVol_Validate,
    /* pfnInstantiate = */      rtVfsChainIsoFsVol_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainIsoFsVol_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainIsoFsVolReg, rtVfsChainIsoFsVolReg);

