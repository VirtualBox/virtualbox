/* $Id: pkzipvfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - PKZIP Virtual Filesystem.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#include <iprt/zip.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/poll.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/stream.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/* See http://www.pkware.com/documents/casestudies/APPNOTE.TXT */

/**
 * PKZip Local File Header.
 */
#pragma pack(1)
typedef struct RTZIPPKZIPLOCALFILEHDR
{
    /** Magic value, see RTZIPPKZIPLOCALFILEHDR_MAGIC. */
    uint32_t                u32Magic;
    /** Minimum version needed to extract. */
    uint16_t                u16Version;
    /** General purpose bit flag. */
    uint16_t                fFlags;
    /** Compression method. See RTZIPPKZIP_COMP_METHOD_XXX. */
    uint16_t                u16ComprMethod;
    /** Last modified time, MS-DOS format: HHHHHMMM MMMSSSSS, multiply seconds by 2 */
    uint16_t                u16LastModifiedTime;
    /** Last modified date, MS-DOS format: YYYYYYYM MMMDDDDD, year starts at 1980 */
    uint16_t                u16LastModifiedDate;
    /** Checksum. */
    uint32_t                u32Crc;
    /** Compressed size. */
    uint32_t                cbCompressed;
    /** Uncompressed size. */
    uint32_t                cbUncompressed;
    /** Length of the file name. */
    uint16_t                cbFilename;
    /** Length of the extra field. */
    uint16_t                cbExtra;
    /** Start of the file name. */
    uint8_t                 u8Filename;
} RTZIPPKZIPLOCALFILEHDR;
#pragma pack()
AssertCompileSize(RTZIPPKZIPLOCALFILEHDR, 30+1);
/** Pointer to PKZip Local File Header. */
typedef RTZIPPKZIPLOCALFILEHDR *PRTZIPPKZIPLOCALFILEHDR;

#define RTZIPPKZIPLOCALFILEHDR_MAGIC        RT_MAKE_U32_FROM_U8('P','K','\003','\004')

/**
 * PKZip compression method.
 */
typedef enum RTZIPPKZIP_COMP_METHOD
{
    /** No compression */
    RTZIPPKZIP_COMP_METHOD_STORED = 0,
    /** Shrunk */
    RTZIPPKZIP_COMP_METHOD_SHRUNK = 1,
    /** Reduced with compression factor 1 */
    RTZIPPKZIP_COMP_METHOD_REDUCED1 = 2,
    /** Reduced with compression factor 2 */
    RTZIPPKZIP_COMP_METHOD_REDUCED2 = 3,
    /** Reduced with compression factor 3 */
    RTZIPPKZIP_COMP_METHOD_REDUCED3 = 4,
    /** Reduced with compression factor 4 */
    RTZIPPKZIP_COMP_METHOD_REDUCED4 = 5,
    /** Imploded */
    RTZIPPKZIP_COMP_METHOD_IMPLODED = 6,
    /** Deflated */
    RTZIPPKZIP_COMP_METHOD_DEFLATED = 8,
    /** Deflated64 */
    RTZIPPKZIP_COMP_METHOD_DEFLATED64 = 9,
    /* Compressed using bzip2 */
    RTZIPPKZIP_COMP_METHOD_BZIP2 = 12,
    /** Compressed using LZMA */
    RTZIPPKZIP_COMP_METHOD_LZMA = 14
} RTZIPPKZIP_COMP_METHOD;

/**
 * PKZip Central Directory Header.
 */
#pragma pack(1)
typedef struct RTZIPPKZIPCENTRDIRHDR
{
    /** The magic value. See RTZIPPKZIPCENTRDIRHDR_MAGIC. */
    uint32_t                u32Magic;
    /** The version used for creating the item. */
    uint16_t                u16VerMade;
    /** The minimum version required for extracting the item. */
    uint16_t                u16VerRequired;
    /** General purpose flags. */
    uint16_t                fFlags;
    /** Compresstion method. See RTZIPPKZIP_COMP_METHOD_XXX */
    uint16_t                u16ComprMethod;
    /** Last modified time, MS-DOS format: HHHHHMMM MMMSSSSS, multiply seconds by 2 */
    uint16_t                u16LastModifiedTime;
    /** Last modified date, MS-DOS format: YYYYYYYM MMMDDDDD, year starts at 1980 */
    uint16_t                u16LastModifiedDate;
    /** Checksum. */
    uint32_t                u32Crc;
    /** Compressed size. */
    uint32_t                cbCompressed;
    /** Uncompressed size. */
    uint32_t                cbUncompressed;
    /** Length of the object file name. */
    uint16_t                cbFilename;
    /** Length of the extra field. */
    uint16_t                cbExtra;
    /** Length of the object comment. */
    uint16_t                cbComment;
    /** The number of the disk on which this file begins. */
    uint16_t                iDiskStart;
    /** Internal attributes. */
    uint16_t                u16IntAttrib;
    /** External attributes. */
    uint32_t                u32ExtAttrib;
    /** Offset from the start of the first disk on which this file appears to
     * where the local file header should be found. */
    uint32_t                offLocalFileHeader;
    /** Start of the file name. */
    uint8_t                 u8Filename;
} RTZIPPKZIPCENTRDIRHDR;
#pragma pack()
AssertCompileSize(RTZIPPKZIPCENTRDIRHDR, 46+1);
/** Pointer to the PKZip Central Directory Header. */
typedef RTZIPPKZIPCENTRDIRHDR *PRTZIPPKZIPCENTRDIRHDR;

#define RTZIPPKZIPCENTRDIRHDR_MAGIC         RT_MAKE_U32_FROM_U8('P','K','\001','\002')

/**
 * PKZip End of Central Directory Record.
 */
#pragma pack(1)
typedef struct RTZIPPKZIPENDOFCENTRDIRREC
{
    /** The magic value. See RTZIPPKZIPENDOFCENTRDIRREC_MAGIC. */
    uint32_t                u32Magic;
    /** Number of this disk. */
    uint16_t                iThisDisk;
    /** Number of the disk with the start of the Central Directory. */
    uint16_t                iDiskStartCentrDirectory;
    /** Number of Central Directory entries on this disk. */
    uint16_t                cCentrDirRecordsThisDisk;
    /** Number of Central Directory records. */
    uint16_t                cCentrDirRecords;
    /** Size of the Central Directory in bytes. */
    uint32_t                cbCentrDir;
    /** Offset of the Central Directory. */
    uint32_t                offCentrDir;
    /** Size of the comment in bytes. */
    uint16_t                cbComment;
    /** Start of the comment. */
    uint8_t                 u8Comment;
} RTZIPPKZIPENDOFCENTRDIRREC;
#pragma pack()
AssertCompileSize(RTZIPPKZIPENDOFCENTRDIRREC, 22+1);
/** Pointer to the PKZip End of Central Directory Record. */
typedef RTZIPPKZIPENDOFCENTRDIRREC const *PCRTZIPPKZIPENDOFCENTRDIRREC;

#define RTZIPPKZIPENDOFCENTRDIRREC_MAGIC    RT_MAKE_U32_FROM_U8('P','K','\005','\006')

/**
 * PKZip ZIP64 End of Central Directory Record.
 */
#pragma pack(1)
typedef struct RTZIPPKZIP64ENDOFCENTRDIRREC
{
    /** The magic value. See RTZIPPKZIP64ENDOFCENTRDIRREC_MAGIC. */
    uint32_t                u32Magic;
    /** Size of Zip64 end of Central Directory Record. */
    uint64_t                cbSizeEocdr;
    /** The version used for creating the item. */
    uint16_t                u16VerMade;
    /** The minimum version required for extracting the item. */
    uint16_t                u16VerRequired;
    /** Number of this disk. */
    uint32_t                iThisDisk;
    /** Number of the disk with the start of the Central Directory. */
    uint32_t                iDiskStartCentrDirectory;
    /** Number of Central Directory entries on this disk. */
    uint64_t                cCentrDirRecordsThisDisk;
    /** Number of Central Directory records. */
    uint64_t                cCentrDirRecords;
    /** Size of the Central Directory in bytes. */
    uint64_t                cbCentrDir;
    /** Offset of the Central Directory. */
    uint64_t                offCentrDir;
} RTZIPPKZIP64ENDOFCENTRDIRREC;
#pragma pack()
AssertCompileSize(RTZIPPKZIP64ENDOFCENTRDIRREC, 56);
/** Pointer to the 64-bit PKZip End of Central Directory Record. */
typedef RTZIPPKZIP64ENDOFCENTRDIRREC *PRTZIPPKZIP64ENDOFCENTRDIRREC;

#define RTZIPPKZIP64ENDOFCENTRDIRREC_MAGIC  RT_MAKE_U32_FROM_U8('P','K','\006','\006')

/**
 * PKZip ZIP64 End of Central Directory Locator.
 */
#pragma pack(1)
typedef struct RTZIPPKZIP64ENDOFCENTRDIRLOC
{
    /** The magic value. See RTZIPPKZIP64ENDOFCENTRDIRLOC_MAGIC. */
    uint32_t                u32Magic;
    /** Number of the disk with the start of the ZIP64 End of Central Directory. */
    uint32_t                iDiskStartCentrDir;
    /** Relative offset of the ZIP64 End of Central Directory Record. */
    uint64_t                offEndOfCentrDirRec;
    /** Total number of disks. */
    uint32_t                cDisks;
} RTZIPPKZIP64ENDOFCENTRDIRLOC;
#pragma pack()
AssertCompileSize(RTZIPPKZIP64ENDOFCENTRDIRLOC, 20);

#define RTZIPPKZIP64ENDOFCENTRDIRLOC_MAGIC  RT_MAKE_U32_FROM_U8('P','K','\006','\007')

/**
 * PKZip ZIP64 Extended Information Extra Field.
 */
#pragma pack(1)
typedef struct RTZIPPKZIP64EXTRAFIELD
{
    /** Uncompressed size. */
    uint64_t                cbUncompressed;
    /** Compressed size. */
    uint64_t                cbCompressed;
    /** Offset from the start of the first disk on which this file appears to
     * where the local file header should be found. */
    uint64_t                offLocalFileHeader;
    /** The number of the disk on which this file begins. */
    uint32_t                iDiskStart;
} RTZIPPKZIP64EXTRAFIELD;
#pragma pack()
/** Pointer to the ZIP64 Extended Information Extra Field. */
typedef RTZIPPKZIP64EXTRAFIELD *PRTZIPPKZIP64EXTRAFIELD;
AssertCompileSize(RTZIPPKZIP64EXTRAFIELD, 28);

/**
 * PKZip reader instance data.
 */
typedef struct RTZIPPKZIPREADER
{
    /** Set if we have the End of Central Directory record. */
    bool                    fHaveEocd;
    /** The Central Directory header. */
    RTZIPPKZIPCENTRDIRHDR   cdh;
    /** ZIP64 extended information. */
    RTZIPPKZIP64EXTRAFIELD  cd64ex;
    /** Set if ZIP64 End of Central Directory Locator is present (archive setting). */
    bool                    fZip64Eocd;
    /** Set if cd64ex is valid for the current file header (object setting). */
    bool                    fZip64Ex;
    /* The name of the current object. */
    char                    szName[RTPATH_MAX];
} RTZIPPKZIPREADER;
/** Pointer to the PKZip reader instance data. */
typedef RTZIPPKZIPREADER *PRTZIPPKZIPREADER;

/**
 * Pkzip object (directory).
 */
typedef struct RTZIPPKZIPBASEOBJ
{
    /** Pointer to the reader instance data (resides in the filesystem
     * stream). */
    PRTZIPPKZIPREADER       pPkzipReader;
    /** The object info with unix attributes. */
    RTFSOBJINFO             ObjInfo;
} RTZIPPKZIPBASEOBJ;
/** Pointer to a PKZIP filesystem stream base object. */
typedef RTZIPPKZIPBASEOBJ *PRTZIPPKZIPBASEOBJ;

/**
 * Pkzip object (file) represented as a VFS I/O stream.
 */
typedef struct RTZIPPKZIPIOSTREAM
{
    /** The basic PKZIP object data. */
    RTZIPPKZIPBASEOBJ       BaseObj;
    /** The number of (uncompressed) bytes in the file. */
    uint64_t                cbFile;
    /** The current file position at uncompressed file data. */
    uint64_t                offFile;
    /** The start position of the compressed data in the hVfsIos. */
    uint64_t                offCompStart;
    /** The current position for decompressing bytes in the hVfsIos. */
    uint64_t                offComp;
    /** The number of compressed bytes starting at offCompStart. */
    uint64_t                cbComp;
    /** Set if we have to pass the type function the next time the input
     * function is called. */
    bool                    fPassZipType;
    /** Set if we've reached the end of the file. */
    bool                    fEndOfStream;
    /** Pkzip compression method for this object. */
    RTZIPPKZIP_COMP_METHOD  enmCompMethod;
    /** Zip compression method. */
    RTZIPTYPE               enmZipType;
    /** The decompressor instance. */
    PRTZIPDECOMP            pZip;
    /** The input I/O stream. */
    RTVFSIOSTREAM           hVfsIos;
} RTZIPPKZIPIOSTREAM;
/** Pointer to a the private data of a PKZIP file I/O stream. */
typedef RTZIPPKZIPIOSTREAM *PRTZIPPKZIPIOSTREAM;


/**
 * PKZip filesystem stream private data. The stream must be seekable!
 */
typedef struct RTZIPPKZIPFSSTREAM
{
    /** The input I/O stream. */
    RTVFSIOSTREAM           hVfsIos;

    /** The current object (referenced). */
    RTVFSOBJ                hVfsCurObj;
    /** Pointer to the private data if hVfsCurObj is representing a file. */
    PRTZIPPKZIPIOSTREAM     pCurIosData;

    /** The offset of the first Central Directory header. */
    uint64_t                offFirstCdh;
    /** The offset of the next Central Directory header. */
    uint64_t                offNextCdh;

    /** Size of the central directory. */
    uint64_t                cbCentrDir;
    /** Current central directory entry. */
    uint64_t                iCentrDirEntry;
    /** Number of central directory entries. */
    uint64_t                cCentrDirEntries;

    /** Set if we have the End of Central Directory Record. */
    bool                    fHaveEocd;
    /** Set if we've reached the end of the stream. */
    bool                    fEndOfStream;
    /** Set if we've encountered a fatal error. */
    int                     rcFatal;

    /** The PKZIP reader instance data. */
    RTZIPPKZIPREADER        PkzipReader;
} RTZIPPKZIPFSSTREAM;
/** Pointer to a the private data of a PKZIP filesystem stream. */
typedef RTZIPPKZIPFSSTREAM *PRTZIPPKZIPFSSTREAM;



/**
 * Decode date/time from DOS format as used in PKZip.
 */
static int rtZipPkzipReaderDecodeDosTime(PRTTIMESPEC pTimeSpec, uint16_t u16Time, uint16_t u16Date)
{
    RTTIME time;
    RT_ZERO(time);
    time.i32Year    = ((u16Date & 0xfe00) >>  9) + 1980;
    time.u8Month    =  (u16Date & 0x01e0) >>  5;
    time.u8MonthDay =   u16Date & 0x001f;
    time.u8Hour     =  (u16Time & 0xf800) >> 11;
    time.u8Minute   =  (u16Time & 0x07e0) >>  5;
    time.u8Second   =   u16Time & 0x001f;
    RTTimeNormalize(&time);
    RTTimeImplode(pTimeSpec, &time);
    return VINF_SUCCESS;
}


/**
 * Parse the Local File Header.
 * Just skip the data as we trust the Central Directory.
 */
static int rtZipPkzipParseLocalFileHeader(PRTZIPPKZIPREADER pThis, PRTZIPPKZIPLOCALFILEHDR pLfh, size_t *pcbExtra)
{
    RT_NOREF_PV(pThis);

    if (pLfh->cbFilename >= sizeof(pThis->szName))
        return VERR_PKZIP_NAME_TOO_LONG;

    *pcbExtra = pLfh->cbFilename + pLfh->cbExtra;
    return VINF_SUCCESS;
}


/**
 * Parse the Central Directory Header.
 */
static int rtZipPkzipParseCentrDirHeader(PRTZIPPKZIPREADER pThis, PRTZIPPKZIPCENTRDIRHDR pCdh, size_t *pcbExtra)
{
    if (pCdh->u32Magic != RTZIPPKZIPCENTRDIRHDR_MAGIC)
        return VERR_PKZIP_BAD_CDF_HEADER;

    if (pCdh->cbFilename >= sizeof(pThis->szName))
        return VERR_PKZIP_NAME_TOO_LONG;

    *pcbExtra = pCdh->cbFilename + pCdh->cbExtra + pCdh->cbComment;

    pThis->cdh = *pCdh;
    pThis->fZip64Ex = false;
    return VINF_SUCCESS;
}


/**
 * Return the offset of the Local File Header.
 */
static uint64_t rtZipPkzipReaderOffLocalHeader(PRTZIPPKZIPREADER pThis)
{
    if (pThis->fZip64Ex && pThis->cdh.offLocalFileHeader == (uint32_t)-1)
        return pThis->cd64ex.offLocalFileHeader;

    return pThis->cdh.offLocalFileHeader;
}


/**
 * Return the uncompressed object size.
 */
static uint64_t rtZipPkzipReaderUncompressed(PRTZIPPKZIPREADER pThis)
{
    if (pThis->fZip64Ex && pThis->cdh.cbUncompressed == (uint32_t)-1)
        return pThis->cd64ex.cbUncompressed;

    return pThis->cdh.cbUncompressed;
}


/**
 * Return the compressed object size.
 */
static uint64_t rtZipPkzipReaderCompressed(PRTZIPPKZIPREADER pThis)
{
    if (pThis->fZip64Ex && pThis->cdh.cbCompressed == (uint32_t)-1)
        return pThis->cd64ex.cbCompressed;

    return pThis->cdh.cbCompressed;
}


/**
 * Parse the extra part of the Central Directory Header.
 */
static int rtZipPkzipParseCentrDirHeaderExtra(PRTZIPPKZIPREADER pThis, uint8_t *pu8Buf, size_t cb,
                                              RTZIPPKZIP_COMP_METHOD *penmCompMethod, uint64_t *pcbCompressed)
{
    int rc = RTStrCopyEx(pThis->szName, sizeof(pThis->szName), (const char*)pu8Buf, pThis->cdh.cbFilename);
    if (RT_SUCCESS(rc))
    {
        pu8Buf += pThis->cdh.cbFilename;
        cb      = pThis->cdh.cbExtra;
        while (cb >= 4)
        {
            uint16_t idExtra = *(uint16_t*)pu8Buf;
            pu8Buf += 2;
            uint16_t cbExtra = *(uint16_t*)pu8Buf;
            pu8Buf += 2;
            cb     -= 4;

            if (cb >= cbExtra)
            {
                switch (idExtra)
                {
                    case 0x0001:
                        /*
                         * ZIP64 Extended Information Extra Field.
                         */
                        if (!pThis->fZip64Eocd)
                            return VERR_PKZIP_ZIP64EX_IN_ZIP32;
                        /* Not all fields are really used. */
                        RT_ZERO(pThis->cd64ex);
                        memcpy(&pThis->cd64ex, pu8Buf, cbExtra);
                        pThis->fZip64Ex = true;
                        break;

                    default:
                        /* unknown, just skip */
                        break;
                }
                pu8Buf += cbExtra;
                cb     -= cbExtra;
            }
            else
            {
                rc = VERR_PKZIP_BAD_CDF_HEADER;
                break;
            }
        }

        *penmCompMethod = (RTZIPPKZIP_COMP_METHOD)pThis->cdh.u16ComprMethod;
        *pcbCompressed  = rtZipPkzipReaderCompressed(pThis);
    }
    return rc;
}


/**
 * Translate a PKZip header to an IPRT object info structure.
 */
static int rtZipPkzipReaderGetFsObjInfo(PRTZIPPKZIPREADER pThis, PRTFSOBJINFO pObjInfo)
{
    /*
     * Zap the whole structure, this takes care of unused space in the union.
     */
    RT_ZERO(*pObjInfo);
    pObjInfo->cbObject = rtZipPkzipReaderUncompressed(pThis);
    pObjInfo->cbAllocated = rtZipPkzipReaderUncompressed(pThis); /* XXX */
    RTTIMESPEC ts;
    rtZipPkzipReaderDecodeDosTime(&ts, pThis->cdh.u16LastModifiedTime, pThis->cdh.u16LastModifiedDate);
    pObjInfo->ChangeTime = ts;
    pObjInfo->ModificationTime = ts;
    pObjInfo->AccessTime = ts;
    pObjInfo->BirthTime = ts;
    const char *pszEnd = strchr(pThis->szName, '\0');
    if (pszEnd == &pThis->szName[0] || pszEnd[-1] != '/')
        pObjInfo->Attr.fMode = RTFS_TYPE_FILE \
                             | RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR \
                             | RTFS_UNIX_IRGRP \
                             | RTFS_UNIX_IROTH;
    else
        pObjInfo->Attr.fMode = RTFS_TYPE_DIRECTORY \
                             | RTFS_UNIX_IRWXU \
                             | RTFS_UNIX_IRGRP | RTFS_UNIX_IXGRP \
                             | RTFS_UNIX_IROTH | RTFS_UNIX_IXOTH;
    pObjInfo->Attr.enmAdditional = RTFSOBJATTRADD_UNIX;
    pObjInfo->Attr.u.Unix.cHardlinks = 1;

    return VINF_SUCCESS;
}


/**
 * Search the magic value of the End Of Central Directory Record.
 *
 * @returns true if found, false otherwise.
 * @param   pu8Buf      buffer.
 * @param   cb          size of buffer.
 * @param   piPos       where to store the position of the magic value.
 */
static bool rtZipPkzipReaderScanEocd(const uint8_t *pu8Buf, size_t cb, int *piPos)
{
    if (cb < 4)
        return false;
    ssize_t i;
    for (i = (ssize_t)cb - 4; i >= 0; --i)
        if (*(uint32_t*)(pu8Buf + i) == RTZIPPKZIPENDOFCENTRDIRREC_MAGIC)
        {
            *piPos = i;
            return true;
        }
    return false;
}


/**
 * Read the Local File Header. We ignore the content -- we trust the Central
 * Directory.
 */
static int rtZipPkzipFssIosReadLfh(PRTZIPPKZIPFSSTREAM pThis, uint64_t *poffStartData)
{
    RTZIPPKZIPLOCALFILEHDR lfh;
    uint64_t offLocalFileHeader = rtZipPkzipReaderOffLocalHeader(&pThis->PkzipReader);
    int rc = RTVfsIoStrmReadAt(pThis->hVfsIos, offLocalFileHeader,
                               &lfh, sizeof(lfh) - 1, true /*fBlocking*/, NULL);
    if (RT_SUCCESS(rc))
    {
        if (lfh.u32Magic == RTZIPPKZIPLOCALFILEHDR_MAGIC)
        {
            size_t cbExtra = 0;
            rc = rtZipPkzipParseLocalFileHeader(&pThis->PkzipReader, &lfh, &cbExtra);
            if (RT_SUCCESS(rc))
            {
                /* Just skip the file name and and extra field. We use the data
                 * from the Central Directory Header. */
                rc = RTVfsIoStrmSkip(pThis->hVfsIos, cbExtra);
                if (RT_SUCCESS(rc))
                    *poffStartData = offLocalFileHeader + sizeof(lfh) - 1 + cbExtra;
            }
        }
        else
            rc = VERR_PKZIP_BAD_LF_HEADER;
    }

    return rc;
}


/**
 * Scan the current Central Directory Header.
 */
static int rtZipPkzipFssIosReadCdh(PRTZIPPKZIPFSSTREAM pThis, uint64_t *poffStartData,
                                    RTZIPPKZIP_COMP_METHOD *penmCompMethod, uint64_t *pcbCompressed)
{
    int rc;

    uint64_t offCd = pThis->offNextCdh - pThis->offFirstCdh;
    if (   pThis->iCentrDirEntry < pThis->cCentrDirEntries
        || offCd + sizeof(RTZIPPKZIPCENTRDIRHDR) - 1 <= pThis->cbCentrDir)
    {
        RTZIPPKZIPCENTRDIRHDR cdh;
        rc = RTVfsIoStrmReadAt(pThis->hVfsIos, pThis->offNextCdh,
                               &cdh, sizeof(cdh) - 1, true /*fBlocking*/, NULL);
        if (RT_SUCCESS(rc))
        {
            pThis->offNextCdh += sizeof(cdh) - 1;
            pThis->iCentrDirEntry++;
            size_t cbExtra = 0;
            rc = rtZipPkzipParseCentrDirHeader(&pThis->PkzipReader, &cdh, &cbExtra);
            if (RT_SUCCESS(rc))
            {
                if (offCd + sizeof(RTZIPPKZIPCENTRDIRHDR) - 1 + cbExtra <= pThis->cbCentrDir)
                {
                    /* extra data up to 64k */
                    uint8_t *pu8Buf = (uint8_t*)RTMemTmpAlloc(cbExtra);
                    if (pu8Buf)
                    {
                        rc = RTVfsIoStrmRead(pThis->hVfsIos, pu8Buf, cbExtra, true /*fBlocking*/, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            rc = rtZipPkzipParseCentrDirHeaderExtra(&pThis->PkzipReader, pu8Buf, cbExtra,
                                                                    penmCompMethod, pcbCompressed);
                            if (RT_SUCCESS(rc))
                                rc = rtZipPkzipFssIosReadLfh(pThis, poffStartData);
                        }
                        pThis->offNextCdh += cbExtra;
                        RTMemTmpFree(pu8Buf);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
                else
                    rc = VERR_EOF;
            }
        }
    }
    else
        rc = VERR_EOF;

    return rc;
}


/**
 * Scan for the End of Central Directory Record. Of course this works not if
 * the stream is non-seekable (i.e. a pipe).
 */
static int rtZipPkzipFssIosReadEocb(PRTZIPPKZIPFSSTREAM pThis)
{
    RTFSOBJINFO Info;
    int rc = RTVfsIoStrmQueryInfo(pThis->hVfsIos, &Info, RTFSOBJATTRADD_UNIX);
    if (RT_FAILURE(rc))
        return rc;

    uint64_t cbFile = Info.cbObject;
    if (cbFile < sizeof(RTZIPPKZIPENDOFCENTRDIRREC)-1)
        return VERR_PKZIP_NO_EOCB;

    /* search for start of the 'end of Central Directory Record' */
    size_t cbBuf = RT_MIN(_1K, cbFile);
    uint8_t *pu8Buf = (uint8_t*)RTMemTmpAlloc(cbBuf);
    if (!pu8Buf)
        return VERR_NO_MEMORY;

    /* maximum size of EOCD comment 2^16-1 */
    const size_t cbHdrMax = 0xffff + sizeof(RTZIPPKZIPENDOFCENTRDIRREC) - 1;
    uint64_t offMin = cbFile >= cbHdrMax ? cbFile - cbHdrMax : 0;

    uint64_t off = cbFile - cbBuf;
    while (off >= offMin)
    {
        rc = RTVfsIoStrmReadAt(pThis->hVfsIos, off, pu8Buf, cbBuf, true /*fBlocking*/, NULL);
        if (RT_FAILURE(rc))
            break;
        int offMagic;
        if (rtZipPkzipReaderScanEocd(pu8Buf, cbBuf, &offMagic))
        {
            off += offMagic;
            RTZIPPKZIPENDOFCENTRDIRREC eocd;
            rc = RTVfsIoStrmReadAt(pThis->hVfsIos, off, &eocd, sizeof(eocd) - 1,
                                   true /*fBlocking*/, NULL);
            if (RT_SUCCESS(rc))
            {
                /* well, this shouldn't fail if the content didn't change */
                if (eocd.u32Magic == RTZIPPKZIPENDOFCENTRDIRREC_MAGIC)
                {
                    /* sanity check */
                    if (off + RT_UOFFSETOF(RTZIPPKZIPENDOFCENTRDIRREC, u8Comment) + eocd.cbComment == cbFile)
                    {
                        pThis->offFirstCdh = eocd.offCentrDir;
                        pThis->offNextCdh = eocd.offCentrDir;
                        pThis->iCentrDirEntry = 0;
                        pThis->cCentrDirEntries = eocd.cCentrDirRecords;
                        pThis->cbCentrDir = eocd.cbCentrDir;
                        pThis->PkzipReader.fHaveEocd = true;
                    }
                    else
                        rc = VERR_PKZIP_NO_EOCB;
                }
                else
                    rc = VERR_PKZIP_NO_EOCB;
            }
            if (rc != VERR_PKZIP_NO_EOCB)
                break;
        }
        else
            rc = VERR_PKZIP_NO_EOCB;
        /* overlap the following read */
        off -= cbBuf - 4;
    }

    RTMemTmpFree(pu8Buf);

    /*
     * Now check for the presence of the Zip64 End of Central Directory Locator.
     */
    if (   RT_SUCCESS(rc)
        && off > (unsigned)sizeof(RTZIPPKZIP64ENDOFCENTRDIRLOC))
    {
        off -= sizeof(RTZIPPKZIP64ENDOFCENTRDIRLOC);

        RTZIPPKZIP64ENDOFCENTRDIRLOC eocd64loc;
        rc = RTVfsIoStrmReadAt(pThis->hVfsIos, off, &eocd64loc, sizeof(eocd64loc), true /*fBlocking*/, NULL);
        if (RT_SUCCESS(rc))
        {
            if (eocd64loc.u32Magic == RTZIPPKZIP64ENDOFCENTRDIRLOC_MAGIC)
                pThis->PkzipReader.fZip64Eocd = true;
        }
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipPkzipFssBaseObj_Close(void *pvThis)
{
    NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipPkzipFssBaseObj_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPPKZIPBASEOBJ pThis = (PRTZIPPKZIPBASEOBJ)pvThis;

    /*
     * Copy the desired data.
     */
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING:
        case RTFSOBJATTRADD_UNIX:
            *pObjInfo = pThis->ObjInfo;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            *pObjInfo = pThis->ObjInfo;
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            *pObjInfo = pThis->ObjInfo;
            break;

        case RTFSOBJATTRADD_EASIZE:
            *pObjInfo = pThis->ObjInfo;
            break;

        default:
            return VERR_NOT_SUPPORTED;
    }

    return VINF_SUCCESS;
}


/**
 * PKZip filesystem base object operations (directory objects).
 */
static const RTVFSOBJOPS g_rtZipPkzipFssBaseObjOps =
{
    RTVFSOBJOPS_VERSION,
    RTVFSOBJTYPE_BASE,
    "PkzipFsStream::Obj",
    rtZipPkzipFssBaseObj_Close,
    rtZipPkzipFssBaseObj_QueryInfo,
    NULL,
    RTVFSOBJOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipPkzipFssIos_Close(void *pvThis)
{
    PRTZIPPKZIPIOSTREAM pThis = (PRTZIPPKZIPIOSTREAM)pvThis;

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    if (pThis->pZip)
    {
        RTZipDecompDestroy(pThis->pZip);
        pThis->pZip = NULL;
    }

    return rtZipPkzipFssBaseObj_Close(&pThis->BaseObj);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipPkzipFssIos_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPPKZIPIOSTREAM pThis = (PRTZIPPKZIPIOSTREAM)pvThis;
    return rtZipPkzipFssBaseObj_QueryInfo(&pThis->BaseObj, pObjInfo, enmAddAttr);
}


/**
 * Callback function for rtZipPkzipFssIos_Read. For feeding compressed data
 * into the decompressor function.
 */
static DECLCALLBACK(int) rtZipPkzipFssIosReadHelper(void *pvThis, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    PRTZIPPKZIPIOSTREAM pThis = (PRTZIPPKZIPIOSTREAM)pvThis;
    int rc = VINF_SUCCESS;
    if (!cbToRead)
        return rc;
    if (pThis->fPassZipType)
    {
        Assert(cbToRead);

        uint8_t *pu8Buf = (uint8_t*)pvBuf;
        pu8Buf[0] = pThis->enmZipType;
        pvBuf = &pu8Buf[1];
        cbToRead--;
        pThis->fPassZipType = false;
    }
    if (cbToRead > 0)
    {
        size_t cbRead = 0;
        const size_t cbAvail = pThis->cbComp;
        rc = RTVfsIoStrmReadAt(pThis->hVfsIos, pThis->offComp, pvBuf,
                               RT_MIN(cbToRead, cbAvail), true /*fBlocking*/, &cbRead);
        if (   RT_SUCCESS(rc)
            && cbToRead > cbAvail)
            rc = pcbRead ? VINF_EOF : VERR_EOF;
        if (   rc == VINF_EOF
            && !pcbRead)
            rc = VERR_EOF;
        pThis->offComp += cbRead;
        if (pcbRead)
            *pcbRead = cbRead;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipPkzipFssIos_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTZIPPKZIPIOSTREAM pThis = (PRTZIPPKZIPIOSTREAM)pvThis;
    Assert(pSgBuf->cSegs == 1);
    RT_NOREF_PV(fBlocking);

    if (off < 0)
        off = pThis->offFile;
    if (off >= (RTFOFF)pThis->cbFile)
        return pcbRead ? VINF_EOF : VERR_EOF;

    Assert(pThis->cbFile >= pThis->offFile);
    uint64_t cbLeft   = (uint64_t)(pThis->cbFile - pThis->offFile);
    size_t   cbToRead = pSgBuf->paSegs[0].cbSeg;
    if (cbToRead > cbLeft)
    {
        if (!pcbRead)
            return VERR_EOF;
        cbToRead = (size_t)cbLeft;
    }

    /*
     * Restart decompression at start of stream or on backward seeking.
     */
    if (   !pThis->pZip
        || !off
        || off < (RTFOFF)pThis->offFile)
    {
        switch (pThis->enmCompMethod)
        {
            case RTZIPPKZIP_COMP_METHOD_STORED:
                pThis->enmZipType = RTZIPTYPE_STORE;
                break;

            case RTZIPPKZIP_COMP_METHOD_DEFLATED:
                pThis->enmZipType = RTZIPTYPE_ZLIB_NO_HEADER;
                break;

            default:
                pThis->enmZipType = RTZIPTYPE_INVALID;
                break;
        }

        if (pThis->pZip)
        {
            RTZipDecompDestroy(pThis->pZip);
            pThis->pZip = NULL;
        }
        int rc = RTZipDecompCreate(&pThis->pZip, (void*)pThis, rtZipPkzipFssIosReadHelper);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Skip bytes if necessary.
     */
    if (off > (RTFOFF)pThis->offFile)
    {
        uint8_t u8Buf[_1K];
        while (off > (RTFOFF)pThis->offFile)
        {
            size_t cbSkip = off - pThis->offFile;
            if (cbSkip > sizeof(u8Buf))
                cbSkip = sizeof(u8Buf);
            int rc = RTZipDecompress(pThis->pZip, u8Buf, cbSkip, NULL);
            if (RT_FAILURE(rc))
                return rc;
            pThis->offFile += cbSkip;
        }
    }

    /*
     * Do the actual reading.
     */
    size_t cbReadStack = 0;
    if (!pcbRead)
        pcbRead = &cbReadStack;
    int rc = RTZipDecompress(pThis->pZip, pSgBuf->paSegs[0].pvSeg, cbToRead, pcbRead);
    pThis->offFile = off + *pcbRead;
    RTSgBufAdvance(pSgBuf, *pcbRead);

    if (pThis->offFile >= pThis->cbFile)
    {
        Assert(pThis->offFile == pThis->cbFile);
        pThis->fEndOfStream = true;
    }

    return rc;
}

static DECLCALLBACK(int) rtZipPkzipFssIos_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    RT_NOREF_PV(pvThis); RT_NOREF_PV(off); RT_NOREF_PV(pSgBuf); RT_NOREF_PV(fBlocking); RT_NOREF_PV(pcbWritten);
    return VERR_NOT_IMPLEMENTED;
}

static DECLCALLBACK(int) rtZipPkzipFssIos_Flush(void *pvThis)
{
    RT_NOREF_PV(pvThis);
    return VERR_NOT_IMPLEMENTED;
}

/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtZipPkzipFssIos_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies,
                                                  bool fIntr, uint32_t *pfRetEvents)
{
    PRTZIPPKZIPIOSTREAM pThis = (PRTZIPPKZIPIOSTREAM)pvThis;

    /* When we've reached the end, read will be set to indicate it. */
    if (   (fEvents & RTPOLL_EVT_READ)
        && pThis->fEndOfStream)
    {
        int rc = RTVfsIoStrmPoll(pThis->hVfsIos, fEvents, 0, fIntr, pfRetEvents);
        if (RT_SUCCESS(rc))
            *pfRetEvents |= RTPOLL_EVT_READ;
        else
            *pfRetEvents = RTPOLL_EVT_READ;
        return VINF_SUCCESS;
    }

    return RTVfsIoStrmPoll(pThis->hVfsIos, fEvents, cMillies, fIntr, pfRetEvents);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtZipPkzipFssIos_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTZIPPKZIPIOSTREAM pThis = (PRTZIPPKZIPIOSTREAM)pvThis;
    *poffActual = pThis->offFile;
    return VINF_SUCCESS;
}


/**
 * Pkzip I/O object stream operations.
 */
static const RTVFSIOSTREAMOPS g_rtZipPkzipFssIosOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "PkzipFsStream::IoStream",
        rtZipPkzipFssIos_Close,
        rtZipPkzipFssIos_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    RTVFSIOSTREAMOPS_FEAT_NO_SG,
    rtZipPkzipFssIos_Read,
    rtZipPkzipFssIos_Write,
    rtZipPkzipFssIos_Flush,
    rtZipPkzipFssIos_PollOne,
    rtZipPkzipFssIos_Tell,
    NULL /*Skip*/,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION
};

/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipPkzipFss_Close(void *pvThis)
{
    PRTZIPPKZIPFSSTREAM pThis = (PRTZIPPKZIPFSSTREAM)pvThis;

    RTVfsObjRelease(pThis->hVfsCurObj);
    pThis->hVfsCurObj  = NIL_RTVFSOBJ;
    pThis->pCurIosData = NULL;

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipPkzipFss_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPPKZIPFSSTREAM pThis = (PRTZIPPKZIPFSSTREAM)pvThis;
    /* Take the lazy approach here, with the sideffect of providing some info
       that is actually kind of useful. */
    return RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnNext}
 */
static DECLCALLBACK(int) rtZipPkzipFss_Next(void *pvThis, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj)
{
    PRTZIPPKZIPFSSTREAM pThis = (PRTZIPPKZIPFSSTREAM)pvThis;

    /*
     * Dispense with the current object.
     */
    if (pThis->hVfsCurObj != NIL_RTVFSOBJ)
    {
        if (pThis->pCurIosData)
        {
            pThis->pCurIosData->fEndOfStream = true;
            pThis->pCurIosData->offFile      = pThis->pCurIosData->cbFile;
            pThis->pCurIosData = NULL;
        }

        RTVfsObjRelease(pThis->hVfsCurObj);
        pThis->hVfsCurObj = NIL_RTVFSOBJ;
    }

    /*
     * Check if we've already reached the end in some way.
     */
    if (pThis->fEndOfStream)
        return VERR_EOF;
    if (pThis->rcFatal != VINF_SUCCESS)
        return pThis->rcFatal;

    int rc = VINF_SUCCESS;

    /*
     * Read the end of Central Directory Record once.
     */
    if (!pThis->PkzipReader.fHaveEocd)
        rc = rtZipPkzipFssIosReadEocb(pThis);
    uint64_t offData = 0;

    /*
     * Parse the current Central Directory Header.
     */
    RTZIPPKZIP_COMP_METHOD enmCompMethod = RTZIPPKZIP_COMP_METHOD_STORED;
    uint64_t cbCompressed = 0;
    if (RT_SUCCESS(rc))
        rc = rtZipPkzipFssIosReadCdh(pThis, &offData, &enmCompMethod, &cbCompressed);
    if (RT_FAILURE(rc))
        return pThis->rcFatal = rc;

    /*
     * Fill an object info structure from the current Pkzip state.
     */
    RTFSOBJINFO Info;
    rc = rtZipPkzipReaderGetFsObjInfo(&pThis->PkzipReader, &Info);
    if (RT_FAILURE(rc))
        return pThis->rcFatal = rc;

    /*
     * Create an object of the appropriate type.
     */
    RTVFSOBJTYPE enmType;
    RTVFSOBJ hVfsObj;
    RTFMODE fType = Info.Attr.fMode & RTFS_TYPE_MASK;
    switch (fType)
    {
        case RTFS_TYPE_FILE:
            RTVFSIOSTREAM hVfsIos;
            PRTZIPPKZIPIOSTREAM pIosData;
            rc = RTVfsNewIoStream(&g_rtZipPkzipFssIosOps,
                                  sizeof(*pIosData),
                                  RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                  NIL_RTVFS,
                                  NIL_RTVFSLOCK,
                                  &hVfsIos,
                                  (void **)&pIosData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pIosData->BaseObj.pPkzipReader = &pThis->PkzipReader;
            pIosData->BaseObj.ObjInfo      = Info;
            pIosData->cbFile               = Info.cbObject;
            pIosData->offFile              = 0;
            pIosData->offComp              = offData;
            pIosData->offCompStart         = offData;
            pIosData->cbComp               = cbCompressed;
            pIosData->enmCompMethod        = enmCompMethod;
            pIosData->fPassZipType         = true;
            pIosData->hVfsIos              = pThis->hVfsIos;
            RTVfsIoStrmRetain(pThis->hVfsIos);
            pThis->pCurIosData = pIosData;
            enmType = RTVFSOBJTYPE_IO_STREAM;
            hVfsObj = RTVfsObjFromIoStream(hVfsIos);
            RTVfsIoStrmRelease(hVfsIos);
            break;

        case RTFS_TYPE_DIRECTORY:
            PRTZIPPKZIPBASEOBJ pBaseObjData;
            rc = RTVfsNewBaseObj(&g_rtZipPkzipFssBaseObjOps,
                                 sizeof(*pBaseObjData),
                                 NIL_RTVFS,
                                 NIL_RTVFSLOCK,
                                 &hVfsObj,
                                 (void **)&pBaseObjData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pBaseObjData->pPkzipReader = &pThis->PkzipReader;
            pBaseObjData->ObjInfo      = Info;
            enmType = RTVFSOBJTYPE_BASE;
            break;

        default:
            return pThis->rcFatal = VERR_PKZIP_UNKNOWN_TYPE_FLAG;
    }
    pThis->hVfsCurObj = hVfsObj;

    if (ppszName)
    {
        rc = RTStrDupEx(ppszName, pThis->PkzipReader.szName);
        if (RT_FAILURE(rc))
            return pThis->rcFatal = rc;
    }

    if (phVfsObj)
    {
        RTVfsObjRetain(hVfsObj);
        *phVfsObj = hVfsObj;
    }

    if (penmType)
        *penmType = enmType;

    return VINF_SUCCESS;
}


/**
 * PKZip filesystem stream operations.
 */
static const RTVFSFSSTREAMOPS rtZipPkzipFssOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_FS_STREAM,
        "PkzipFsStream",
        rtZipPkzipFss_Close,
        rtZipPkzipFss_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSFSSTREAMOPS_VERSION,
    0,
    rtZipPkzipFss_Next,
    NULL,
    NULL,
    NULL,
    RTVFSFSSTREAMOPS_VERSION
};


RTDECL(int) RTZipPkzipFsStreamFromIoStream(RTVFSIOSTREAM hVfsIosIn, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(phVfsFss, VERR_INVALID_HANDLE);
    *phVfsFss = NIL_RTVFSFSSTREAM;
    AssertPtrReturn(hVfsIosIn, VERR_INVALID_HANDLE);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIosIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Retain the input stream and create a new filesystem stream handle.
     */
    PRTZIPPKZIPFSSTREAM pThis;
    RTVFSFSSTREAM     hVfsFss;
    int rc = RTVfsNewFsStream(&rtZipPkzipFssOps, sizeof(*pThis), NIL_RTVFS, NIL_RTVFSLOCK, RTFILE_O_READ,
                              &hVfsFss, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsIos              = hVfsIosIn;
        pThis->hVfsCurObj           = NIL_RTVFSOBJ;
        pThis->pCurIosData          = NULL;
        pThis->fEndOfStream         = false;
        pThis->rcFatal              = VINF_SUCCESS;
        pThis->fHaveEocd            = false;

        *phVfsFss = hVfsFss;
        return VINF_SUCCESS;
    }

    RTVfsIoStrmRelease(hVfsIosIn);
    return rc;
}
