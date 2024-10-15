/* $Id: tarvfswriter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - TAR Virtual Filesystem, Writer.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/zip.h>

#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/zero.h>

#include "tarvfsreader.h"
#include "cpiovfsreader.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The TAR block size we're using in this implementation.
 * @remarks Should technically be user configurable, but we don't currently need that. */
#define RTZIPTAR_BLOCKSIZE      sizeof(RTZIPTARHDR)

/** Minimum file size we consider for sparse files. */
#define RTZIPTAR_MIN_SPARSE     _64K


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * A data span descriptor in a sparse file.
 */
typedef struct RTZIPTARSPARSESPAN
{
    /** Byte offset into the file of the data. */
    uint64_t        off;
    /** Number of bytes of data, rounded up to a multiple of blocksize. */
    uint64_t        cb;
} RTZIPTARSPARSESPAN;
/** Pointer to a data span. */
typedef RTZIPTARSPARSESPAN *PRTZIPTARSPARSESPAN;
/** Pointer to a const data span. */
typedef RTZIPTARSPARSESPAN const *PCRTZIPTARSPARSESPAN;

/**
 * Chunk of TAR sparse file data spans.
 */
typedef struct RTZIPTARSPARSECHUNK
{
    /** List entry. */
    RTLISTNODE          Entry;
    /** Array of data spans. */
    RTZIPTARSPARSESPAN  aSpans[63];
} RTZIPTARSPARSECHUNK;
AssertCompile(sizeof(RTZIPTARSPARSECHUNK) <= 1024);
AssertCompile(sizeof(RTZIPTARSPARSECHUNK) >= 1008);
/** Pointer to a chunk of TAR data spans. */
typedef RTZIPTARSPARSECHUNK *PRTZIPTARSPARSECHUNK;
/** Pointer to a const chunk of TAR data spans. */
typedef RTZIPTARSPARSECHUNK const *PCRTZIPTARSPARSECHUNK;

/**
 * TAR sparse file info.
 */
typedef struct RTZIPTARSPARSE
{
    /** Number of data bytes (real size).  */
    uint64_t            cbDataSpans;
    /** Number of data spans. */
    uint32_t            cDataSpans;
    /** The index of the next span in the tail chunk (to avoid modulus 63). */
    uint32_t            iNextSpan;
    /** Head of the data span chunk list (PRTZIPTARSPARSECHUNK). */
    RTLISTANCHOR        ChunkHead;
} RTZIPTARSPARSE;
/** Pointer to TAR sparse file info. */
typedef RTZIPTARSPARSE *PRTZIPTARSPARSE;
/** Pointer to a const TAR sparse file info. */
typedef RTZIPTARSPARSE const *PCRTZIPTARSPARSE;


/** Pointer to a the private data of a TAR filesystem stream. */
typedef struct RTZIPTARFSSTREAMWRITER *PRTZIPTARFSSTREAMWRITER;


/**
 * Instance data for a file or I/O stream returned by
 * RTVFSFSSTREAMOPS::pfnPushFile.
 */
typedef struct RTZIPTARFSSTREAMWRITERPUSH
{
    /** Pointer to the parent FS stream writer instance.
     * This is set to NULL should the push object live longer than the stream. */
    PRTZIPTARFSSTREAMWRITER pParent;
    /** The header offset, UINT64_MAX if non-seekable output. */
    uint64_t                offHdr;
    /** The data offset, UINT64_MAX if non-seekable output. */
    uint64_t                offData;
    /** The current I/O stream position (relative to offData). */
    uint64_t                offCurrent;
    /** The expected size amount of file content, or max file size if open-ended. */
    uint64_t                cbExpected;
    /** The current amount of file content written. */
    uint64_t                cbCurrent;
    /** Object info copy for rtZipTarWriterPush_QueryInfo. */
    RTFSOBJINFO             ObjInfo;
    /** Set if open-ended file size requiring a tar header update when done. */
    bool                    fOpenEnded;
} RTZIPTARFSSTREAMWRITERPUSH;
/** Pointer to a push I/O instance. */
typedef RTZIPTARFSSTREAMWRITERPUSH *PRTZIPTARFSSTREAMWRITERPUSH;


/**
 * Tar filesystem stream private data.
 */
typedef struct RTZIPTARFSSTREAMWRITER
{
    /** The output I/O stream. */
    RTVFSIOSTREAM           hVfsIos;
    /** Non-nil if the output is a file.  */
    RTVFSFILE               hVfsFile;

    /** The current push file.  NULL if none. */
    PRTZIPTARFSSTREAMWRITERPUSH pPush;

    /** The TAR format. */
    RTZIPTARFORMAT          enmFormat;
    /** Set if we've encountered a fatal error. */
    int                     rcFatal;
    /** Flags, RTZIPTAR_C_XXX. */
    uint32_t                fFlags;

    /** Number of bytes written. */
    uint64_t                cbWritten;

    /** @name Attribute overrides.
     * @{
     */
    RTUID                   uidOwner;           /**< Owner, NIL_RTUID if no change. */
    char                   *pszOwner;           /**< Owner, NULL if no change. */
    RTGID                   gidGroup;           /**< Group, NIL_RTGID if no change. */
    char                   *pszGroup;           /**< Group, NULL if no change. */
    char                   *pszPrefix;          /**< Path prefix, NULL if no change. */
    size_t                  cchPrefix;          /**< The length of pszPrefix. */
    PRTTIMESPEC             pModTime;           /**< Modification time, NULL of no change. */
    RTTIMESPEC              ModTime;            /**< pModTime points to this. */
    RTFMODE                 fFileModeAndMask;   /**< File mode AND mask. */
    RTFMODE                 fFileModeOrMask;    /**< File mode OR mask. */
    RTFMODE                 fDirModeAndMask;    /**< Directory mode AND mask. */
    RTFMODE                 fDirModeOrMask;     /**< Directory mode OR mask. */
    /** @} */

    /** When in update mode (RTZIPTAR_C_UPDATE) we have an reader FSS instance,
     * though w/o the RTVFSFSSTREAM bits. (Allocated after this structure.) */
    PRTZIPTARFSSTREAM       pReadTar;
    /** Same when a CPIO archive is used. */
    /** @todo Possible to merge it with the tar reader maybe? */
    PRTZIPCPIOFSSTREAM      pReadCpio;
    /** Set if we're in writing mode and pfnNext shall fail. */
    bool                    fWriting;


    /** Number of headers returned by rtZipTarFssWriter_ObjInfoToHdr. */
    uint32_t                cHdrs;
    /** Size of a single header returned by rtZipTarFssWriter_ObjInfoToHdr. */
    uint32_t                cbHdr;
    /** Format dependent header data. */
    union
    {
        /** Byte view of the different headers. */
        uint8_t             abHdrs[512];
        /** Header buffers returned by rtZipTarFssWriter_ObjInfoToHdrTar. */
        RTZIPTARHDR         aHdrsTar[3];
        /** CPIO stuff. */
        struct
        {
            /** CPIO header. */
            CPIOHDR         Hdr;
            /** The filepath which comes directly after the header - embedded here
             * to make the code more uniform with the tar code later on. */
            char            achFilePath[_1K];
            /** Maximum padding if the file path is completely used. */
            char            abPad[3];
        } Cpio;
    };
    /** The fallback I/O buffer if memory allocation fails. */
    uint8_t                 abIoBuf[512];
} RTZIPTARFSSTREAMWRITER;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) rtZipTarWriterPush_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual);
static int rtZipTarFssWriter_CompleteCurrentPushFile(PRTZIPTARFSSTREAMWRITER pThis);
static int rtZipTarFssWriter_AddFile(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, RTVFSIOSTREAM hVfsIos,
                                     PCRTFSOBJINFO pObjInfo, const char *pszOwnerNm, const char *pszGroupNm);


/**
 * Calculates the header checksum and stores it in the chksum field.
 *
 * @returns IPRT status code.
 * @param   pHdr                The header.
 */
static int rtZipTarFssWriter_ChecksumHdr(PRTZIPTARHDR pHdr)
{
    int32_t iUnsignedChksum;
    rtZipTarCalcChkSum(pHdr, &iUnsignedChksum, NULL);

    int rc = RTStrFormatU32(pHdr->Common.chksum, sizeof(pHdr->Common.chksum), iUnsignedChksum,
                            8 /*uBase*/, -1 /*cchWidth*/, sizeof(pHdr->Common.chksum) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
    AssertRCReturn(rc, VERR_TAR_NUM_VALUE_TOO_LARGE);
    return VINF_SUCCESS;
}



/**
 * Formats a 12 character wide file offset or size field.
 *
 * This is mainly used for RTZIPTARHDR::Common.size, but also for formatting the
 * sparse map.
 *
 * @returns IPRT status code.
 * @param   pach12Field     The 12 character wide destination field.
 * @param   off             The offset to set.
 */
static int rtZipTarFssWriter_FormatOffset(char pach12Field[12], uint64_t off)
{
    /*
     * Is the size small enough for the standard octal string encoding?
     *
     * Note! We could actually use the terminator character as well if we liked,
     *       but let not do that as it's easier to test this way.
     */
    if (off < _4G * 2U)
    {
        int rc = RTStrFormatU64(pach12Field, 12, off, 8 /*uBase*/, -1 /*cchWidth*/, 12 - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
        AssertRCReturn(rc, rc);
    }
    /*
     * No, use the base 256 extension. Set the highest bit of the left most
     * character.  We don't deal with negatives here, cause the size have to
     * be greater than zero.
     *
     * Note! The base-256 extension are never used by gtar or libarchive
     *       with the "ustar  \0" format version, only the later
     *       "ustar\000" version.  However, this shouldn't cause much
     *       trouble as they are not picky about what they read.
     */
    /** @todo above note is wrong:  GNU tar only uses base-256 with the GNU tar
     * format, i.e. "ustar   \0", see create.c line 303 in v1.29. */
    else
    {
        size_t         cchField  = 12 - 1;
        unsigned char *puchField = (unsigned char *)pach12Field;
        puchField[0] = 0x80;
        do
        {
            puchField[cchField--] = off & 0xff;
            off >>= 8;
        } while (cchField);
    }

    return VINF_SUCCESS;
}


/**
 * Returns a flag whether the writer is writing a tar archive, false if CPIO.
 *
 * @returns Flag whether the writer is in tar mode, true for tar and false for CPIO.
 * @param   pThis           The TAR writer instance.
 */
DECLINLINE(bool) rtZipTarFssWrite_IsTar(PRTZIPTARFSSTREAMWRITER pThis)
{
    return pThis->enmFormat != RTZIPTARFORMAT_CPIO_ASCII_NEW;
}


/**
 * Returns the block size of the given archive format.
 *
 * @returns Block size in bytes.
 * @param   pThis           The TAR writer instance.
 */
DECLINLINE(size_t) rtZipTarFssWrite_GetBlockSize(PRTZIPTARFSSTREAMWRITER pThis)
{
    if (!rtZipTarFssWrite_IsTar(pThis))
        return sizeof(uint32_t);

    return RTZIPTAR_BLOCKSIZE;
}


/**
 * Creates one or more tar headers for the object.
 *
 * Returns RTZIPTARFSSTREAMWRITER::aHdrsTar and RTZIPTARFSSTREAMWRITER::cHdrs.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the file.
 * @param   hVfsIos         The I/O stream of the file.
 * @param   fFlags          The RTVFSFSSTREAMOPS::pfnAdd flags.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 * @param   chType          The tar record type, UINT8_MAX for default.
 */
static int rtZipTarFssWriter_ObjInfoToHdrTar(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, PCRTFSOBJINFO pObjInfo,
                                             const char *pszOwnerNm, const char *pszGroupNm, uint8_t chType)
{
    pThis->cHdrs = 0;
    RT_ZERO(pThis->aHdrsTar[0]);

    /*
     * The path name first.  Make sure to flip DOS slashes.
     */
    size_t cchPath = strlen(pszPath);
    if (cchPath < sizeof(pThis->aHdrsTar[0].Common.name))
    {
        memcpy(pThis->aHdrsTar[0].Common.name, pszPath, cchPath + 1);
#if RTPATH_STYLE != RTPATH_STR_F_STYLE_UNIX
        char *pszDosSlash = strchr(pThis->aHdrsTar[0].Common.name, '\\');
        while (pszDosSlash)
        {
            *pszDosSlash = '/';
            pszDosSlash = strchr(pszDosSlash + 1, '\\');
        }
#endif
    }
    else
    {
        /** @todo implement gnu and pax long name extensions. */
        return VERR_TAR_NAME_TOO_LONG;
    }

    /*
     * File mode.  ASSUME that the unix part of the IPRT mode mask is
     * compatible with the TAR/Unix world.
     */
    uint32_t uValue = pObjInfo->Attr.fMode & RTFS_UNIX_MASK;
    if (RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
        uValue = (uValue & pThis->fDirModeAndMask) | pThis->fDirModeOrMask;
    else
        uValue = (uValue & pThis->fFileModeAndMask) | pThis->fFileModeOrMask;
    int rc = RTStrFormatU32(pThis->aHdrsTar[0].Common.mode, sizeof(pThis->aHdrsTar[0].Common.mode), uValue, 8 /*uBase*/,
                            -1 /*cchWidth*/, sizeof(pThis->aHdrsTar[0].Common.mode) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
    AssertRCReturn(rc, VERR_TAR_NUM_VALUE_TOO_LARGE);

    /*
     * uid & gid.  Just guard against NIL values as they won't fit.
     */
    uValue = pThis->uidOwner != NIL_RTUID ? pThis->uidOwner
           : pObjInfo->Attr.u.Unix.uid != NIL_RTUID ? pObjInfo->Attr.u.Unix.uid : 0;
    rc = RTStrFormatU32(pThis->aHdrsTar[0].Common.uid, sizeof(pThis->aHdrsTar[0].Common.uid), uValue,
                        8 /*uBase*/, -1 /*cchWidth*/, sizeof(pThis->aHdrsTar[0].Common.uid) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
    AssertRCReturn(rc, VERR_TAR_NUM_VALUE_TOO_LARGE);

    uValue = pThis->gidGroup != NIL_RTGID ? pThis->gidGroup
           : pObjInfo->Attr.u.Unix.gid != NIL_RTGID ? pObjInfo->Attr.u.Unix.gid : 0;
    rc = RTStrFormatU32(pThis->aHdrsTar[0].Common.gid, sizeof(pThis->aHdrsTar[0].Common.gid), uValue,
                        8 /*uBase*/, -1 /*cchWidth*/, sizeof(pThis->aHdrsTar[0].Common.gid) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
    AssertRCReturn(rc, VERR_TAR_NUM_VALUE_TOO_LARGE);

    /*
     * The file size.
     */
    rc = rtZipTarFssWriter_FormatOffset(pThis->aHdrsTar[0].Common.size, pObjInfo->cbObject);
    AssertRCReturn(rc, rc);

    /*
     * Modification time relative to unix epoc.
     */
    rc = RTStrFormatU64(pThis->aHdrsTar[0].Common.mtime, sizeof(pThis->aHdrsTar[0].Common.mtime),
                        RTTimeSpecGetSeconds(pThis->pModTime ? pThis->pModTime : &pObjInfo->ModificationTime),
                        8 /*uBase*/, -1 /*cchWidth*/, sizeof(pThis->aHdrsTar[0].Common.mtime) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
    AssertRCReturn(rc, rc);

    /* Skipping checksum for now */

    /*
     * The type flag.
     */
    if (chType == UINT8_MAX)
        switch (pObjInfo->Attr.fMode & RTFS_TYPE_MASK)
        {
            case RTFS_TYPE_FIFO:        chType = RTZIPTAR_TF_FIFO; break;
            case RTFS_TYPE_DEV_CHAR:    chType = RTZIPTAR_TF_CHR; break;
            case RTFS_TYPE_DIRECTORY:   chType = RTZIPTAR_TF_DIR; break;
            case RTFS_TYPE_DEV_BLOCK:   chType = RTZIPTAR_TF_BLK; break;
            case RTFS_TYPE_FILE:        chType = RTZIPTAR_TF_NORMAL; break;
            case RTFS_TYPE_SYMLINK:     chType = RTZIPTAR_TF_SYMLINK; break;
            case RTFS_TYPE_SOCKET:      chType = RTZIPTAR_TF_FIFO; break;
            case RTFS_TYPE_WHITEOUT:    AssertFailedReturn(VERR_WRONG_TYPE);
        }
    pThis->aHdrsTar[0].Common.typeflag = chType;

    /* No link name, at least not for now.  Caller might set it. */

    /*
     * Set TAR record magic and version.
     */
    if (pThis->enmFormat == RTZIPTARFORMAT_GNU)
        memcpy(pThis->aHdrsTar[0].Gnu.magic, RTZIPTAR_GNU_MAGIC, sizeof(pThis->aHdrsTar[0].Gnu.magic));
    else if (   pThis->enmFormat == RTZIPTARFORMAT_USTAR
             || pThis->enmFormat == RTZIPTARFORMAT_PAX)
    {
        memcpy(pThis->aHdrsTar[0].Common.magic, RTZIPTAR_USTAR_MAGIC, sizeof(pThis->aHdrsTar[0].Common.magic));
        memcpy(pThis->aHdrsTar[0].Common.version, RTZIPTAR_USTAR_VERSION, sizeof(pThis->aHdrsTar[0].Common.version));
    }
    else
        AssertFailedReturn(VERR_INTERNAL_ERROR_4);

    /*
     * Owner and group names.  Silently truncate them for now.
     */
    RTStrCopy(pThis->aHdrsTar[0].Common.uname, sizeof(pThis->aHdrsTar[0].Common.uname), pThis->pszOwner ? pThis->pszOwner : pszOwnerNm);
    RTStrCopy(pThis->aHdrsTar[0].Common.gname, sizeof(pThis->aHdrsTar[0].Common.uname), pThis->pszGroup ? pThis->pszGroup : pszGroupNm);

    /*
     * Char/block device numbers.
     */
    if (   RTFS_IS_DEV_BLOCK(pObjInfo->Attr.fMode)
        || RTFS_IS_DEV_CHAR(pObjInfo->Attr.fMode) )
    {
        rc = RTStrFormatU32(pThis->aHdrsTar[0].Common.devmajor, sizeof(pThis->aHdrsTar[0].Common.devmajor),
                            RTDEV_MAJOR(pObjInfo->Attr.u.Unix.Device),
                            8 /*uBase*/, -1 /*cchWidth*/, sizeof(pThis->aHdrsTar[0].Common.devmajor) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
        AssertRCReturn(rc, VERR_TAR_NUM_VALUE_TOO_LARGE);

        rc = RTStrFormatU32(pThis->aHdrsTar[0].Common.devminor, sizeof(pThis->aHdrsTar[0].Common.devmajor),
                            RTDEV_MINOR(pObjInfo->Attr.u.Unix.Device),
                            8 /*uBase*/, -1 /*cchWidth*/, sizeof(pThis->aHdrsTar[0].Common.devmajor) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
        AssertRCReturn(rc, VERR_TAR_NUM_VALUE_TOO_LARGE);
    }

#if 0 /** @todo why doesn't this work? */
    /*
     * Set GNU specific stuff.
     */
    if (pThis->enmFormat == RTZIPTARFORMAT_GNU)
    {
        rc = RTStrFormatU64(pThis->aHdrsTar[0].Gnu.ctime, sizeof(pThis->aHdrsTar[0].Gnu.ctime),
                            RTTimeSpecGetSeconds(&pObjInfo->ChangeTime),
                            8 /*uBase*/, -1 /*cchWidth*/, sizeof(pThis->aHdrsTar[0].Gnu.ctime) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
        AssertRCReturn(rc, rc);

        rc = RTStrFormatU64(pThis->aHdrsTar[0].Gnu.atime, sizeof(pThis->aHdrsTar[0].Gnu.atime),
                            RTTimeSpecGetSeconds(&pObjInfo->ChangeTime),
                            8 /*uBase*/, -1 /*cchWidth*/, sizeof(pThis->aHdrsTar[0].Gnu.atime) - 1, RTSTR_F_ZEROPAD | RTSTR_F_PRECISION);
        AssertRCReturn(rc, rc);
    }
#endif

    /*
     * Finally the checksum.
     */
    pThis->cHdrs = 1;
    pThis->cbHdr = sizeof(pThis->aHdrsTar[0]);
    return rtZipTarFssWriter_ChecksumHdr(&pThis->aHdrsTar[0]);
}


/**
 * Formats a given 32-bit value into a CPIO header field.
 *
 * @param   pach8Field      The field to format the value into.
 * @param   u32             The value to format.
 */
static void rtZipTarFssWriter_CpioFmtU32(char pach8Field[8], uint32_t u32)
{
    static const char s_achHex[] = "0123456789abcdef";
    size_t         cchField  = 7;
    unsigned char *puchField = (unsigned char *)pach8Field;
    do
    {
        puchField[cchField--] = s_achHex[u32 & 0xf];
        u32 >>= 4;
    } while (cchField);

    /* Last one. */
    puchField[0] = s_achHex[u32 & 0xf];
}


/**
 * Creates one or more CPIO headers for the object.
 *
 * Returns RTZIPTARFSSTREAMWRITER::Cpio::Hdr and RTZIPTARFSSTREAMWRITER::cHdrs.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the file.
 * @param   hVfsIos         The I/O stream of the file.
 * @param   fFlags          The RTVFSFSSTREAMOPS::pfnAdd flags.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 */
static int rtZipTarFssWriter_ObjInfoToHdrCpio(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, PCRTFSOBJINFO pObjInfo,
                                              const char *pszOwnerNm, const char *pszGroupNm)
{
    RT_NOREF(pszOwnerNm, pszGroupNm);
    AssertReturn(!rtZipTarFssWrite_IsTar(pThis), VERR_INTERNAL_ERROR_3);

    pThis->cHdrs = 0;
    RT_ZERO(pThis->Cpio.Hdr);
    RT_ZERO(pThis->Cpio.achFilePath);

    /*
     * The path name first.  Make sure to flip DOS slashes.
     */
    size_t cchPath = strlen(pszPath) + 1;
    if (cchPath < sizeof(pThis->Cpio.achFilePath))
    {
        memcpy(&pThis->Cpio.achFilePath[0], pszPath, cchPath);
#if RTPATH_STYLE != RTPATH_STR_F_STYLE_UNIX
        char *pszDosSlash = strchr(&pThis->Cpio.achFilePath[0], '\\');
        while (pszDosSlash)
        {
            *pszDosSlash = '/';
            pszDosSlash = strchr(pszDosSlash + 1, '\\');
        }
#endif
    }
    else
        return VERR_TAR_NAME_TOO_LONG;

    if (pObjInfo->cbObject != (uint32_t)pObjInfo->cbObject)
        return VERR_FILE_TOO_BIG;

    /*
     * Set CPIO record magic and version.
     */
    if (pThis->enmFormat == RTZIPTARFORMAT_CPIO_ASCII_NEW)
        memcpy(pThis->Cpio.Hdr.AsciiNew.achMagic, CPIO_HDR_NEW_MAGIC, sizeof(pThis->Cpio.Hdr.AsciiNew.achMagic));
    else
        AssertFailedReturn(VERR_INTERNAL_ERROR_4);

    /*
     * File mode.  ASSUME that the unix part of the IPRT mode mask is
     * compatible with the TAR/Unix world.
     */
    uint32_t uValue = pObjInfo->Attr.fMode & RTFS_UNIX_MASK;
    if (pObjInfo->Attr.fMode) /* Don't want to modify the EOS record here. */
    {
        if (RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
            uValue = (uValue & pThis->fDirModeAndMask) | pThis->fDirModeOrMask;
        else
            uValue = (uValue & pThis->fFileModeAndMask) | pThis->fFileModeOrMask;
    }
    rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achMode, uValue);

    /*
     * Inode id.
     */
    rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achInode, pObjInfo->Attr.u.Unix.INodeId);

    /*
     * Number of hardlinks.
     */
    rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achNLinks, pObjInfo->Attr.u.Unix.cHardlinks ? pObjInfo->Attr.u.Unix.cHardlinks : 1);

    /*
     * uid & gid.  Just guard against NIL values as they won't fit.
     */
    uValue = pThis->uidOwner != NIL_RTUID ? pThis->uidOwner
           : pObjInfo->Attr.u.Unix.uid != NIL_RTUID ? pObjInfo->Attr.u.Unix.uid : 0;
    rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achUid, uValue);

    uValue = pThis->gidGroup != NIL_RTGID ? pThis->gidGroup
           : pObjInfo->Attr.u.Unix.gid != NIL_RTGID ? pObjInfo->Attr.u.Unix.gid : 0;
    rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achGid, uValue);

    /*
     * The file size.
     */
    rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achFileSize, (uint32_t)pObjInfo->cbObject);

    /*
     * Modification time relative to unix epoc.
     */
    rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achMTime,
                      RTTimeSpecGetSeconds(pThis->pModTime ? pThis->pModTime : &pObjInfo->ModificationTime));

    /*
     * Char/block device numbers.
     */
    if (   RTFS_IS_DEV_BLOCK(pObjInfo->Attr.fMode)
        || RTFS_IS_DEV_CHAR(pObjInfo->Attr.fMode) )
    {
        rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achDevMajor, RTDEV_MAJOR(pObjInfo->Attr.u.Unix.Device));
        rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achDevMajor, RTDEV_MINOR(pObjInfo->Attr.u.Unix.Device));
    }

    /*
     * File path size including the terminator.
     */
    rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achNameSize, (uint32_t)cchPath);

    pThis->cHdrs = 1;
    pThis->cbHdr = RT_ALIGN_32(sizeof(pThis->Cpio.Hdr) + cchPath, 4);
    return VINF_SUCCESS;
}


/**
 * Creates one or more headers for the object.
 *
 * Returns RTZIPTARFSSTREAMWRITER::cbHdrs and RTZIPTARFSSTREAMWRITER::cHdrs.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the file.
 * @param   hVfsIos         The I/O stream of the file.
 * @param   fFlags          The RTVFSFSSTREAMOPS::pfnAdd flags.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 * @param   chType          The tar record type, UINT8_MAX for default.
 */
static int rtZipTarFssWriter_ObjInfoToHdr(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, PCRTFSOBJINFO pObjInfo,
                                          const char *pszOwnerNm, const char *pszGroupNm, uint8_t chType)
{
    if (!rtZipTarFssWrite_IsTar(pThis))
        return rtZipTarFssWriter_ObjInfoToHdrCpio(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm);

    return rtZipTarFssWriter_ObjInfoToHdrTar(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm, chType);
}




/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_Close(void *pvThis)
{
    PRTZIPTARFSSTREAMWRITERPUSH pPush   = (PRTZIPTARFSSTREAMWRITERPUSH)pvThis;
    PRTZIPTARFSSTREAMWRITER     pParent = pPush->pParent;
    if (pParent)
    {
        if (pParent->pPush == pPush)
            rtZipTarFssWriter_CompleteCurrentPushFile(pParent);
        else
            AssertFailedStmt(pPush->pParent = NULL);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPTARFSSTREAMWRITERPUSH pPush = (PRTZIPTARFSSTREAMWRITERPUSH)pvThis;

    /* Basic info (w/ additional unix attribs). */
    *pObjInfo = pPush->ObjInfo;
    pObjInfo->cbObject = pPush->cbCurrent;
    pObjInfo->cbAllocated = RT_ALIGN_64(pPush->cbCurrent, rtZipTarFssWrite_GetBlockSize(pPush->pParent));

    /* Additional info. */
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING:
        case RTFSOBJATTRADD_UNIX:
            Assert(pObjInfo->Attr.enmAdditional == RTFSOBJATTRADD_UNIX);
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.u.UnixOwner.uid = pPush->ObjInfo.Attr.u.Unix.uid;
            if (   pPush->pParent
                && rtZipTarFssWrite_IsTar(pPush->pParent))
                strcpy(pObjInfo->Attr.u.UnixOwner.szName, pPush->pParent->aHdrsTar[0].Common.uname);
            else
                pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            pObjInfo->Attr.enmAdditional = enmAddAttr;
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid = pPush->ObjInfo.Attr.u.Unix.gid;
            if (   pPush->pParent
                && rtZipTarFssWrite_IsTar(pPush->pParent))
                strcpy(pObjInfo->Attr.u.UnixGroup.szName, pPush->pParent->aHdrsTar[0].Common.uname);
            else
                pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            pObjInfo->Attr.enmAdditional = enmAddAttr;
            break;

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.u.EASize.cb = 0;
            pObjInfo->Attr.enmAdditional = enmAddAttr;
            break;

        default:
        AssertFailed();
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    /* No read support, sorry. */
    RT_NOREF(pvThis, off, pSgBuf, fBlocking, pcbRead);
    AssertFailed();
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTZIPTARFSSTREAMWRITERPUSH pPush   = (PRTZIPTARFSSTREAMWRITERPUSH)pvThis;
    PRTZIPTARFSSTREAMWRITER     pParent = pPush->pParent;
    AssertPtrReturn(pParent, VERR_WRONG_ORDER);

    int rc = pParent->rcFatal;
    AssertRCReturn(rc, rc);

    /*
     * Single segment at a time.
     */
    Assert(pSgBuf->cSegs == 1);
    size_t      cbToWrite = pSgBuf->paSegs[0].cbSeg;
    void const *pvToWrite = pSgBuf->paSegs[0].pvSeg;

    /*
     * Hopefully we don't need to seek.  But if we do, let the seek method do
     * it as it's not entirely trivial.
     */
    if (   off < 0
        || (uint64_t)off == pPush->offCurrent)
        rc = VINF_SUCCESS;
    else
        rc = rtZipTarWriterPush_Seek(pvThis, off, RTFILE_SEEK_BEGIN, NULL);
    if (RT_SUCCESS(rc))
    {
        Assert(pPush->offCurrent <= pPush->cbExpected);
        Assert(pPush->offCurrent <= pPush->cbCurrent);
        AssertMsgReturn(cbToWrite <= pPush->cbExpected - pPush->offCurrent,
                        ("offCurrent=%#RX64 + cbToWrite=%#zx = %#RX64; cbExpected=%RX64\n",
                         pPush->offCurrent, cbToWrite, pPush->offCurrent + cbToWrite, pPush->cbExpected),
                        VERR_DISK_FULL);
        size_t cbWritten = 0;
        rc = RTVfsIoStrmWrite(pParent->hVfsIos, pvToWrite, cbToWrite, fBlocking, &cbWritten);
        if (RT_SUCCESS(rc))
        {
            pPush->offCurrent += cbWritten;
            if (pPush->offCurrent > pPush->cbCurrent)
            {
                pParent->cbWritten = pPush->offCurrent - pPush->cbCurrent;
                pPush->cbCurrent   = pPush->offCurrent;
            }
            if (pcbWritten)
                *pcbWritten = cbWritten;
            RTSgBufAdvance(pSgBuf, cbWritten);
        }
    }

    /*
     * Fatal errors get down here, non-fatal ones returns earlier.
     */
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;
    pParent->rcFatal = rc;
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_Flush(void *pvThis)
{
    PRTZIPTARFSSTREAMWRITERPUSH pPush   = (PRTZIPTARFSSTREAMWRITERPUSH)pvThis;
    PRTZIPTARFSSTREAMWRITER     pParent = pPush->pParent;
    AssertPtrReturn(pParent, VERR_WRONG_ORDER);
    int rc = pParent->rcFatal;
    if (RT_SUCCESS(rc))
        pParent->rcFatal = rc = RTVfsIoStrmFlush(pParent->hVfsIos);
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                                    uint32_t *pfRetEvents)
{
    PRTZIPTARFSSTREAMWRITERPUSH pPush = (PRTZIPTARFSSTREAMWRITERPUSH)pvThis;
    PRTZIPTARFSSTREAMWRITER     pParent = pPush->pParent;
    AssertPtrReturn(pParent, VERR_WRONG_ORDER);
    return RTVfsIoStrmPoll(pParent->hVfsIos, fEvents, cMillies, fIntr, pfRetEvents);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTZIPTARFSSTREAMWRITERPUSH pPush = (PRTZIPTARFSSTREAMWRITERPUSH)pvThis;
    *poffActual = (RTFOFF)pPush->offCurrent;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnSkip}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_Skip(void *pvThis, RTFOFF cb)
{
    RT_NOREF(pvThis, cb);
    AssertFailed();
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    RT_NOREF(pvThis, fMode, fMask);
    AssertFailed();
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                     PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RT_NOREF(pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    AssertFailed();
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    RT_NOREF(pvThis, uid, gid);
    AssertFailed();
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTZIPTARFSSTREAMWRITERPUSH pPush   = (PRTZIPTARFSSTREAMWRITERPUSH)pvThis;
    PRTZIPTARFSSTREAMWRITER     pParent = pPush->pParent;
    AssertPtrReturn(pParent, VERR_WRONG_ORDER);

    int rc = pParent->rcFatal;
    AssertRCReturn(rc, rc);
    Assert(pPush->offCurrent <= pPush->cbCurrent);

    /*
     * Calculate the new file offset.
     */
    RTFOFF offNewSigned;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offNewSigned = offSeek;
            break;
        case RTFILE_SEEK_CURRENT:
            offNewSigned = pPush->offCurrent + offSeek;
            break;
        case RTFILE_SEEK_END:
            offNewSigned = pPush->cbCurrent + offSeek;
            break;
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    /*
     * Check the new file offset against expectations.
     */
    AssertMsgReturn(offNewSigned >= 0, ("offNewSigned=%RTfoff\n", offNewSigned), VERR_NEGATIVE_SEEK);

    uint64_t offNew = (uint64_t)offNewSigned;
    AssertMsgReturn(offNew <= pPush->cbExpected, ("offNew=%#RX64 cbExpected=%#Rx64\n", offNew, pPush->cbExpected), VERR_SEEK);

    /*
     * Any change at all?  We can always hope...
     */
    if (offNew == pPush->offCurrent)
    { }
    /*
     * Gap that needs zero filling?
     */
    else if (offNew > pPush->cbCurrent)
    {
        if (pPush->offCurrent != pPush->cbCurrent)
        {
            AssertReturn(pParent->hVfsFile != NIL_RTVFSFILE, VERR_NOT_A_FILE);
            rc = RTVfsFileSeek(pParent->hVfsFile, pPush->offData + pPush->cbCurrent, RTFILE_SEEK_BEGIN, NULL);
            if (RT_FAILURE(rc))
                return pParent->rcFatal = rc;
            pPush->offCurrent = pPush->cbCurrent;
        }

        uint64_t cbToZero = offNew - pPush->cbCurrent;
        rc = RTVfsIoStrmZeroFill(pParent->hVfsIos, cbToZero);
        if (RT_FAILURE(rc))
            return pParent->rcFatal = rc;
        pParent->cbWritten += cbToZero;
        pPush->cbCurrent = pPush->offCurrent = offNew;
    }
    /*
     * Just change the file position to somewhere we've already written.
     */
    else
    {
        AssertReturn(pParent->hVfsFile != NIL_RTVFSFILE, VERR_NOT_A_FILE);
        rc = RTVfsFileSeek(pParent->hVfsFile, pPush->offData + offNew, RTFILE_SEEK_BEGIN, NULL);
        if (RT_FAILURE(rc))
            return pParent->rcFatal = rc;
        pPush->offCurrent = offNew;
    }
    Assert(pPush->offCurrent <= pPush->cbCurrent);

    if (poffActual)
        *poffActual = pPush->offCurrent;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtZipTarWriterPush_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTZIPTARFSSTREAMWRITERPUSH pPush = (PRTZIPTARFSSTREAMWRITERPUSH)pvThis;
    *pcbFile = pPush->cbCurrent;
    return VINF_SUCCESS;
}


/**
 * TAR writer push I/O stream operations.
 */
DECL_HIDDEN_CONST(const RTVFSIOSTREAMOPS) g_rtZipTarWriterIoStrmOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "TAR push I/O Stream",
        rtZipTarWriterPush_Close,
        rtZipTarWriterPush_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    RTVFSIOSTREAMOPS_FEAT_NO_SG,
    rtZipTarWriterPush_Read,
    rtZipTarWriterPush_Write,
    rtZipTarWriterPush_Flush,
    rtZipTarWriterPush_PollOne,
    rtZipTarWriterPush_Tell,
    rtZipTarWriterPush_Skip,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION,
};


/**
 * TAR writer push file operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_rtZipTarWriterFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "TAR push file",
            rtZipTarWriterPush_Close,
            rtZipTarWriterPush_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtZipTarWriterPush_Read,
        rtZipTarWriterPush_Write,
        rtZipTarWriterPush_Flush,
        rtZipTarWriterPush_PollOne,
        rtZipTarWriterPush_Tell,
        rtZipTarWriterPush_Skip,
        NULL /*ZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtZipTarWriterPush_SetMode,
        rtZipTarWriterPush_SetTimes,
        rtZipTarWriterPush_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtZipTarWriterPush_Seek,
    rtZipTarWriterPush_QuerySize,
    NULL /*SetSize*/,
    NULL /*QueryMaxSize*/,
    RTVFSFILEOPS_VERSION
};



/**
 * Checks rcFatal and completes any current push file.
 *
 * On return the output stream position will be at the next header location.
 *
 * After this call, the push object no longer can write anything.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 */
static int rtZipTarFssWriter_CompleteCurrentPushFile(PRTZIPTARFSSTREAMWRITER pThis)
{
    /*
     * Check if there is a push file pending, remove it if there is.
     * We also check for fatal errors at this point so the caller doesn't need to.
     */
    PRTZIPTARFSSTREAMWRITERPUSH pPush = pThis->pPush;
    if (!pPush)
    {
        AssertRC(pThis->rcFatal);
        return pThis->rcFatal;
    }

    pThis->pPush   = NULL;
    pPush->pParent = NULL;

    int rc = pThis->rcFatal;
    AssertRCReturn(rc, rc);

    /*
     * Do we need to update the header.  pThis->aHdrsTar[0] will retain the current
     * content at pPush->offHdr and we only need to update the size.
     */
    if (pPush->fOpenEnded)
    {
        if (rtZipTarFssWrite_IsTar(pThis))
        {
            rc = rtZipTarFssWriter_FormatOffset(pThis->aHdrsTar[0].Common.size, pPush->cbCurrent);
            if (RT_SUCCESS(rc))
                rc = rtZipTarFssWriter_ChecksumHdr(&pThis->aHdrsTar[0]);
        }
        else
        {
            if (pPush->cbCurrent == (uint32_t)pPush->cbCurrent)
                rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achFileSize, (uint32_t)pPush->cbCurrent);
            else
                rc = VERR_FILE_TOO_BIG;
        }

        if (RT_SUCCESS(rc))
        {
            rc = RTVfsFileWriteAt(pThis->hVfsFile, pPush->offHdr, &pThis->abHdrs[0], pThis->cbHdr, NULL);
            if (RT_SUCCESS(rc))
                rc = RTVfsFileSeek(pThis->hVfsFile, pPush->offData + pPush->cbCurrent, RTFILE_SEEK_BEGIN, NULL);
        }
    }
    /*
     * Check that we've received all the data we were promissed in the PushFile
     * call, fail if we weren't.
     */
    else
        AssertMsgStmt(pPush->cbCurrent == pPush->cbExpected,
                      ("cbCurrent=%#RX64 cbExpected=%#RX64\n", pPush->cbCurrent, pPush->cbExpected),
                      rc = VERR_BUFFER_UNDERFLOW);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do zero padding if necessary.
         */
        size_t cbBlock = rtZipTarFssWrite_GetBlockSize(pThis);
        if (pPush->cbCurrent & (cbBlock - 1))
        {
            size_t cbToZero = cbBlock - (pPush->cbCurrent & (cbBlock - 1));
            rc = RTVfsIoStrmWrite(pThis->hVfsIos, g_abRTZero4K, cbToZero, true /*fBlocking*/, NULL);
            if (RT_SUCCESS(rc))
                pThis->cbWritten += cbToZero;
        }
    }

    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;
    pThis->rcFatal = rc;
    return rc;
}


/**
 * Does the actual work for rtZipTarFssWriter_SwitchToWriteMode().
 *
 * @note    We won't be here if we've truncate the tar file.   Truncation
 *          switches it into write mode.
 */
DECL_NO_INLINE(static, int) rtZipTarFssWriter_SwitchToWriteModeSlow(PRTZIPTARFSSTREAMWRITER pThis)
{
    /* Always go thru rtZipTarFssWriter_SwitchToWriteMode(). */
    AssertRCReturn(pThis->rcFatal, pThis->rcFatal);
    AssertReturn(!pThis->fWriting, VINF_SUCCESS);
    AssertReturn(pThis->fFlags & RTZIPTAR_C_UPDATE, VERR_INTERNAL_ERROR_3);

    /*
     * If we're not at the end, locate the end of the tar file.
     * Because I'm lazy, we do that using rtZipTarFss_Next.  This isn't entirely
     * optimial as it involves VFS object instantations and such.
     */
    /** @todo Optimize skipping to end of tar file in update mode. */
    while (!pThis->pReadTar->fEndOfStream)
    {
        int rc = rtZipTarFss_Next(pThis->pReadTar, NULL, NULL, NULL);
        if (rc == VERR_EOF)
            break;
        AssertRCReturn(rc, rc);
    }

    /*
     * Seek to the desired cut-off point and indicate that we've switched to writing.
     */
    Assert(pThis->pReadTar->offNextHdr == pThis->pReadTar->offCurHdr);
    int rc = RTVfsFileSeek(pThis->hVfsFile, pThis->pReadTar->offNextHdr, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
    if (RT_SUCCESS(rc))
        pThis->fWriting = true;
    else
        pThis->rcFatal = rc;

    return rc;
}


/**
 * Switches the stream into writing mode if necessary.
 *
 * @returns VBox status code.
 * @param   pThis           The TAR writer instance.
 *
 */
DECLINLINE(int) rtZipTarFssWriter_SwitchToWriteMode(PRTZIPTARFSSTREAMWRITER pThis)
{
    if (pThis->fWriting)
        return VINF_SUCCESS; /* ASSUMES caller already checked pThis->rcFatal. */
    return rtZipTarFssWriter_SwitchToWriteModeSlow(pThis);
}


/**
 * Allocates a buffer for transfering file data.
 *
 * @note    Will use the 3rd TAR header as fallback buffer if we're out of
 *          memory!
 *
 * @returns Pointer to buffer (won't ever fail).
 * @param   pThis           The TAR writer instance.
 * @param   pcbBuf          Where to return the buffer size.  This will be a
 *                          multiple of the TAR block size.
 * @param   ppvFree         Where to return the pointer to pass to RTMemTmpFree
 *                          when done with the buffer.
 * @param   cbFile          The file size.  Used as a buffer size hint.
 */
static uint8_t *rtZipTarFssWriter_AllocBuf(PRTZIPTARFSSTREAMWRITER pThis, size_t *pcbBuf, void **ppvFree, uint64_t cbObject)
{
    uint8_t *pbBuf;

    /*
     * If this is a large file, try for a large buffer with 16KB alignment.
     */
    if (cbObject >= _64M)
    {
        pbBuf = (uint8_t *)RTMemTmpAlloc(_2M + _16K - 1);
        if (pbBuf)
        {
            *pcbBuf  = _2M;
            *ppvFree = pbBuf;
            return RT_ALIGN_PT(pbBuf, _16K, uint8_t *);
        }
    }
    /*
     * 4KB aligned 512KB buffer if larger 512KB or larger.
     */
    else if (cbObject >= _512K)
    {
        pbBuf = (uint8_t *)RTMemTmpAlloc(_512K + _4K - 1);
        if (pbBuf)
        {
            *pcbBuf  = _512K;
            *ppvFree = pbBuf;
            return RT_ALIGN_PT(pbBuf, _4K, uint8_t *);
        }
    }
    /*
     * Otherwise a 4KB aligned 128KB buffer.
     */
    else
    {
        pbBuf = (uint8_t *)RTMemTmpAlloc(_128K + _4K - 1);
        if (pbBuf)
        {
            *pcbBuf  = _128K;
            *ppvFree = pbBuf;
            return RT_ALIGN_PT(pbBuf, _4K, uint8_t *);
        }
    }

    /*
     * If allocation failed, fallback on a 16KB buffer without any extra alignment.
     */
    pbBuf = (uint8_t *)RTMemTmpAlloc(_16K);
    if (pbBuf)
    {
        *pcbBuf  = _16K;
        *ppvFree = pbBuf;
        return pbBuf;
    }

    /*
     * Final fallback, 512B buffer using the fallback buffer.
     */
    *pcbBuf  = sizeof(pThis->abIoBuf);
    *ppvFree = NULL;
    return &pThis->abIoBuf[0];
}


/**
 * Frees the sparse info for a TAR file.
 *
 * @param   pSparse         The sparse info to free.
 */
static void rtZipTarFssWriter_SparseInfoDestroy(PRTZIPTARSPARSE pSparse)
{
    PRTZIPTARSPARSECHUNK pCur;
    PRTZIPTARSPARSECHUNK pNext;
    RTListForEachSafe(&pSparse->ChunkHead, pCur, pNext, RTZIPTARSPARSECHUNK, Entry)
        RTMemTmpFree(pCur);
    RTMemTmpFree(pSparse);
}


/**
 * Adds a data span to the sparse info.
 *
 * @returns VINF_SUCCESS or VINF_NO_TMP_MEMORY.
 * @param   pSparse         The sparse info to free.
 * @param   offSpan         Offset of the span.
 * @param   cbSpan          Number of bytes.
 */
static int rtZipTarFssWriter_SparseInfoAddSpan(PRTZIPTARSPARSE pSparse, uint64_t offSpan, uint64_t cbSpan)
{
    /*
     * Get the chunk we're adding it to.
     */
    PRTZIPTARSPARSECHUNK pChunk;
    if (pSparse->iNextSpan != 0)
    {
        pChunk = RTListGetLast(&pSparse->ChunkHead, RTZIPTARSPARSECHUNK, Entry);
        Assert(pSparse->iNextSpan < RT_ELEMENTS(pChunk->aSpans));
    }
    else
    {
        pChunk = (PRTZIPTARSPARSECHUNK)RTMemTmpAllocZ(sizeof(*pChunk));
        if (!pChunk)
            return VERR_NO_TMP_MEMORY;
        RTListAppend(&pSparse->ChunkHead, &pChunk->Entry);
    }

    /*
     * Append it.
     */
    pSparse->cDataSpans  += 1;
    pSparse->cbDataSpans += cbSpan;
    pChunk->aSpans[pSparse->iNextSpan].cb  = cbSpan;
    pChunk->aSpans[pSparse->iNextSpan].off = offSpan;
    if (++pSparse->iNextSpan >= RT_ELEMENTS(pChunk->aSpans))
        pSparse->iNextSpan = 0;
    return VINF_SUCCESS;
}


/**
 * Scans the input stream recording non-zero blocks.
 */
static int rtZipTarFssWriter_ScanSparseFile(PRTZIPTARFSSTREAMWRITER pThis, RTVFSFILE hVfsFile, uint64_t cbFile,
                                            size_t cbBuf, uint8_t *pbBuf, PRTZIPTARSPARSE *ppSparse)
{
    RT_NOREF(pThis);

    /*
     * Create an empty sparse info bundle.
     */
    PRTZIPTARSPARSE pSparse = (PRTZIPTARSPARSE)RTMemTmpAlloc(sizeof(*pSparse));
    AssertReturn(pSparse, VERR_NO_MEMORY);
    pSparse->cbDataSpans = 0;
    pSparse->cDataSpans  = 0;
    pSparse->iNextSpan   = 0;
    RTListInit(&pSparse->ChunkHead);

    /*
     * Scan the file from the start.
     */
    int rc = RTVfsFileSeek(hVfsFile, 0, RTFILE_SEEK_BEGIN, NULL);
    if (RT_SUCCESS(rc))
    {
        bool        fZeroSpan = false;
        uint64_t    offSpan   = 0;
        uint64_t    cbSpan    = 0;

        for (uint64_t off = 0; off < cbFile;)
        {
            uint64_t cbLeft   = cbFile - off;
            size_t   cbToRead = cbLeft >= cbBuf ? cbBuf : (size_t)cbLeft;
            rc = RTVfsFileRead(hVfsFile, pbBuf, cbToRead, NULL);
            if (RT_FAILURE(rc))
                break;
            size_t cBlocks = cbToRead / RTZIPTAR_BLOCKSIZE;

            /* Zero pad the final buffer to a multiple of the blocksize. */
            if (!(cbToRead & (RTZIPTAR_BLOCKSIZE - 1)))
            { /* likely */ }
            else
            {
                AssertBreakStmt(cbLeft == cbToRead, rc = VERR_INTERNAL_ERROR_3);
                RT_BZERO(&pbBuf[cbToRead], RTZIPTAR_BLOCKSIZE - (cbToRead & (RTZIPTAR_BLOCKSIZE - 1)));
                cBlocks++;
            }

            /*
             * Process the blocks we've just read one by one.
             */
            uint8_t const *pbBlock = pbBuf;
            for (size_t iBlock = 0; iBlock < cBlocks; iBlock++)
            {
                bool fZeroBlock = ASMMemIsZero(pbBlock, RTZIPTAR_BLOCKSIZE);
                if (fZeroBlock == fZeroSpan)
                    cbSpan += RTZIPTAR_BLOCKSIZE;
                else
                {
                    if (!fZeroSpan && cbSpan)
                    {
                        rc = rtZipTarFssWriter_SparseInfoAddSpan(pSparse, offSpan, cbSpan);
                        if (RT_FAILURE(rc))
                            break;
                    }
                    fZeroSpan = fZeroBlock;
                    offSpan   = off;
                    cbSpan    = RTZIPTAR_BLOCKSIZE;
                }

                /* next block. */
                pbBlock += RTZIPTAR_BLOCKSIZE;
                off     += RTZIPTAR_BLOCKSIZE;
            }
        }

        /*
         * Deal with the final span.  If we've got zeros thowards the end, we
         * must add a zero byte data span at the end.
         */
        if (RT_SUCCESS(rc))
        {
            if (!fZeroSpan && cbSpan)
            {
                if (cbFile & (RTZIPTAR_BLOCKSIZE - 1))
                {
                    Assert(!(cbSpan & (RTZIPTAR_BLOCKSIZE - 1)));
                    cbSpan -= RTZIPTAR_BLOCKSIZE;
                    cbSpan |= cbFile & (RTZIPTAR_BLOCKSIZE - 1);
                }
                rc = rtZipTarFssWriter_SparseInfoAddSpan(pSparse, offSpan, cbSpan);
            }
            if (RT_SUCCESS(rc))
                rc = rtZipTarFssWriter_SparseInfoAddSpan(pSparse, cbFile, 0);
        }
    }

    if (RT_SUCCESS(rc))
    {
        /*
         * Return the file back to the start position before we return so that we
         * can segue into the regular rtZipTarFssWriter_AddFile without further ado.
         */
        rc = RTVfsFileSeek(hVfsFile, 0, RTFILE_SEEK_BEGIN, NULL);
        if (RT_SUCCESS(rc))
        {
            *ppSparse = pSparse;
            return VINF_SUCCESS;
        }
    }

    rtZipTarFssWriter_SparseInfoDestroy(pSparse);
    *ppSparse = NULL;
    return rc;
}


/**
 * Writes GNU the sparse file headers.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the file.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 * @param   pSparse         The sparse file info.
 */
static int rtZipTarFssWriter_WriteGnuSparseHeaders(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath,  PCRTFSOBJINFO pObjInfo,
                                                   const char *pszOwnerNm, const char *pszGroupNm, PCRTZIPTARSPARSE pSparse)
{
    /*
     * Format the first header.
     */
    int rc = rtZipTarFssWriter_ObjInfoToHdrTar(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm, RTZIPTAR_TF_GNU_SPARSE);
    AssertRCReturn(rc, rc);
    AssertReturn(pThis->cHdrs == 1, VERR_INTERNAL_ERROR_2);

    /* data size. */
    rc = rtZipTarFssWriter_FormatOffset(pThis->aHdrsTar[0].Common.size, pSparse->cbDataSpans);
    AssertRCReturn(rc, rc);

    /* realsize. */
    rc = rtZipTarFssWriter_FormatOffset(pThis->aHdrsTar[0].Gnu.realsize, pObjInfo->cbObject);
    AssertRCReturn(rc, rc);

    Assert(pThis->aHdrsTar[0].Gnu.isextended == 0);

    /*
     * Walk the sparse spans, fill and write headers one by one.
     */
    PRTZIPTARGNUSPARSE  paSparse    = &pThis->aHdrsTar[0].Gnu.sparse[0];
    uint32_t            cSparse     = RT_ELEMENTS(pThis->aHdrsTar[0].Gnu.sparse);
    uint32_t            iSparse     = 0;

    PRTZIPTARSPARSECHUNK const pLastChunk = RTListGetLast(&pSparse->ChunkHead, RTZIPTARSPARSECHUNK, Entry);
    PRTZIPTARSPARSECHUNK pChunk;
    RTListForEach(&pSparse->ChunkHead, pChunk, RTZIPTARSPARSECHUNK, Entry)
    {
        uint32_t cSpans = pChunk != pLastChunk || pSparse->iNextSpan == 0
                        ? RT_ELEMENTS(pChunk->aSpans) : pSparse->iNextSpan;
        for (uint32_t iSpan = 0; iSpan < cSpans; iSpan++)
        {
            /* Flush the header? */
            if (iSparse >= cSparse)
            {
                if (cSparse != RT_ELEMENTS(pThis->aHdrsTar[0].Gnu.sparse))
                    pThis->aHdrsTar[0].GnuSparse.isextended = 1; /* more headers to come */
                else
                {
                    pThis->aHdrsTar[0].Gnu.isextended = 1; /* more headers to come */
                    rc = rtZipTarFssWriter_ChecksumHdr(&pThis->aHdrsTar[0]);
                }
                if (RT_SUCCESS(rc))
                    rc = RTVfsIoStrmWrite(pThis->hVfsIos, &pThis->aHdrsTar[0], sizeof(pThis->aHdrsTar[0]), true /*fBlocking*/, NULL);
                if (RT_FAILURE(rc))
                    return rc;
                RT_ZERO(pThis->aHdrsTar[0]);
                cSparse  = RT_ELEMENTS(pThis->aHdrsTar[0].GnuSparse.sp);
                iSparse  = 0;
                paSparse = &pThis->aHdrsTar[0].GnuSparse.sp[0];
            }

            /* Append sparse data segment. */
            rc = rtZipTarFssWriter_FormatOffset(paSparse[iSparse].offset, pChunk->aSpans[iSpan].off);
            AssertRCReturn(rc, rc);
            rc = rtZipTarFssWriter_FormatOffset(paSparse[iSparse].numbytes, pChunk->aSpans[iSpan].cb);
            AssertRCReturn(rc, rc);
            iSparse++;
        }
    }

    /*
     * The final header.
     */
    if (iSparse != 0)
    {
        if (cSparse != RT_ELEMENTS(pThis->aHdrsTar[0].Gnu.sparse))
            Assert(pThis->aHdrsTar[0].GnuSparse.isextended == 0);
        else
        {
            Assert(pThis->aHdrsTar[0].Gnu.isextended == 0);
            rc = rtZipTarFssWriter_ChecksumHdr(&pThis->aHdrsTar[0]);
        }
        if (RT_SUCCESS(rc))
            rc = RTVfsIoStrmWrite(pThis->hVfsIos, &pThis->aHdrsTar[0], sizeof(pThis->aHdrsTar[0]), true /*fBlocking*/, NULL);
    }
    pThis->cHdrs = 0;
    return rc;
}


/**
 * Adds a potentially sparse file to the output.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the file.
 * @param   hVfsFile        The potentially sparse file.
 * @param   hVfsIos         The I/O stream of the file. Same as @a hVfsFile.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 */
static int rtZipTarFssWriter_AddFileSparse(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, RTVFSFILE hVfsFile,
                                           RTVFSIOSTREAM hVfsIos, PCRTFSOBJINFO pObjInfo,
                                           const char *pszOwnerNm, const char *pszGroupNm)
{
    AssertReturn(rtZipTarFssWrite_IsTar(pThis), VERR_INTERNAL_ERROR_2);

    /*
     * Scan the input file to locate all zero blocks.
     */
    void    *pvBufFree;
    size_t   cbBuf;
    uint8_t *pbBuf = rtZipTarFssWriter_AllocBuf(pThis, &cbBuf, &pvBufFree, pObjInfo->cbObject);

    PRTZIPTARSPARSE pSparse;
    int rc = rtZipTarFssWriter_ScanSparseFile(pThis, hVfsFile, pObjInfo->cbObject, cbBuf, pbBuf, &pSparse);
    if (RT_SUCCESS(rc))
    {
        /*
         * If there aren't at least 2 zero blocks in the file, don't bother
         * doing the sparse stuff and store it as a normal file.
         */
        if (pSparse->cbDataSpans + RTZIPTAR_BLOCKSIZE > (uint64_t)pObjInfo->cbObject)
        {
            rtZipTarFssWriter_SparseInfoDestroy(pSparse);
            RTMemTmpFree(pvBufFree);
            return rtZipTarFssWriter_AddFile(pThis, pszPath, hVfsIos, pObjInfo, pszOwnerNm, pszGroupNm);
        }

        /*
         * Produce and write the headers.
         */
        if (pThis->enmFormat == RTZIPTARFORMAT_GNU)
            rc = rtZipTarFssWriter_WriteGnuSparseHeaders(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm, pSparse);
        else
            AssertStmt(pThis->enmFormat != RTZIPTARFORMAT_GNU, rc = VERR_NOT_IMPLEMENTED);
        if (RT_SUCCESS(rc))
        {
            /*
             * Write the file bytes.
             */
            PRTZIPTARSPARSECHUNK const pLastChunk = RTListGetLast(&pSparse->ChunkHead, RTZIPTARSPARSECHUNK, Entry);
            PRTZIPTARSPARSECHUNK pChunk;
            RTListForEach(&pSparse->ChunkHead, pChunk, RTZIPTARSPARSECHUNK, Entry)
            {
                uint32_t cSpans = pChunk != pLastChunk || pSparse->iNextSpan == 0
                                ? RT_ELEMENTS(pChunk->aSpans) : pSparse->iNextSpan;
                for (uint32_t iSpan = 0; iSpan < cSpans; iSpan++)
                {
                    rc = RTVfsFileSeek(hVfsFile, pChunk->aSpans[iSpan].off, RTFILE_SEEK_BEGIN, NULL);
                    if (RT_FAILURE(rc))
                        break;
                    uint64_t cbLeft = pChunk->aSpans[iSpan].cb;
                    Assert(   !(cbLeft & (RTZIPTAR_BLOCKSIZE - 1))
                           || (iSpan + 1 == cSpans && pChunk == pLastChunk));
                    while (cbLeft > 0)
                    {
                        size_t cbToRead = cbLeft >= cbBuf ? cbBuf : (size_t)cbLeft;
                        rc = RTVfsFileRead(hVfsFile, pbBuf, cbToRead, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            rc = RTVfsIoStrmWrite(pThis->hVfsIos, pbBuf, cbToRead, true /*fBlocking*/, NULL);
                            if (RT_SUCCESS(rc))
                            {
                                pThis->cbWritten += cbToRead;
                                cbLeft           -= cbToRead;
                                continue;
                            }
                        }
                        break;
                    }
                    if (RT_FAILURE(rc))
                        break;
                }
            }

            /*
             * Do the zero padding.
             */
            if (   RT_SUCCESS(rc)
                && (pSparse->cbDataSpans & (RTZIPTAR_BLOCKSIZE - 1)))
            {
                size_t cbToZero = RTZIPTAR_BLOCKSIZE - (pSparse->cbDataSpans & (RTZIPTAR_BLOCKSIZE - 1));
                rc = RTVfsIoStrmWrite(pThis->hVfsIos, g_abRTZero4K, cbToZero, true /*fBlocking*/, NULL);
                if (RT_SUCCESS(rc))
                    pThis->cbWritten += cbToZero;
            }
        }

        if (RT_FAILURE(rc))
            pThis->rcFatal = rc;
        rtZipTarFssWriter_SparseInfoDestroy(pSparse);
    }
    RTMemTmpFree(pvBufFree);
    return rc;
}


/**
 * Adds an I/O stream of indeterminate length to the TAR file.
 *
 * This requires the output to be seekable, i.e. a file, because we need to go
 * back and update @c size field of the TAR header after pumping all the data
 * bytes thru and establishing the file length.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the file.
 * @param   hVfsIos         The I/O stream of the file.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 */
static int rtZipTarFssWriter_AddFileStream(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, RTVFSIOSTREAM hVfsIos,
                                           PCRTFSOBJINFO pObjInfo, const char *pszOwnerNm, const char *pszGroupNm)
{
    AssertReturn(pThis->hVfsFile != NIL_RTVFSFILE, VERR_NOT_A_FILE);

    /*
     * Append the header.
     */
    int rc = rtZipTarFssWriter_ObjInfoToHdr(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm, UINT8_MAX);
    if (RT_SUCCESS(rc))
    {
        RTFOFF const offHdr = RTVfsFileTell(pThis->hVfsFile);
        if (offHdr >= 0)
        {
            rc = RTVfsIoStrmWrite(pThis->hVfsIos, pThis->abHdrs, pThis->cHdrs * pThis->cbHdr, true /*fBlocking*/, NULL);
            if (RT_SUCCESS(rc))
            {
                pThis->cbWritten += pThis->cHdrs * pThis->cbHdr;

                /*
                 * Transfer the bytes.
                 */
                void    *pvBufFree;
                size_t   cbBuf;
                uint8_t *pbBuf = rtZipTarFssWriter_AllocBuf(pThis, &cbBuf, &pvBufFree,
                                                            pObjInfo->cbObject > 0 && pObjInfo->cbObject != RTFOFF_MAX
                                                            ? pObjInfo->cbObject : _1G);

                uint64_t cbReadTotal = 0;
                for (;;)
                {
                    size_t cbRead = 0;
                    int rc2 = rc = RTVfsIoStrmRead(hVfsIos, pbBuf, cbBuf, true /*fBlocking*/, &cbRead);
                    if (RT_SUCCESS(rc))
                    {
                        cbReadTotal += cbRead;
                        rc = RTVfsIoStrmWrite(pThis->hVfsIos, pbBuf, cbRead, true /*fBlocking*/, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            pThis->cbWritten += cbRead;
                            if (rc2 != VINF_EOF)
                                continue;
                        }
                    }
                    Assert(rc != VERR_EOF /* expecting VINF_EOF! */);
                    break;
                }

                RTMemTmpFree(pvBufFree);

                /*
                 * Do the zero padding.
                 */
                size_t cbBlock = rtZipTarFssWrite_GetBlockSize(pThis);
                if ((cbReadTotal & (cbBlock - 1)) && RT_SUCCESS(rc))
                {
                    size_t cbToZero = cbBlock - (cbReadTotal & (cbBlock - 1));
                    rc = RTVfsIoStrmWrite(pThis->hVfsIos, g_abRTZero4K, cbToZero, true /*fBlocking*/, NULL);
                    if (RT_SUCCESS(rc))
                        pThis->cbWritten += cbToZero;
                }

                /*
                 * Update the header.  We ASSUME that aHdr[0] is unmodified
                 * from before the data pumping above and just update the size.
                 */
                if ((RTFOFF)cbReadTotal != pObjInfo->cbObject && RT_SUCCESS(rc))
                {
                    RTFOFF const offRestore = RTVfsFileTell(pThis->hVfsFile);
                    if (offRestore >= 0)
                    {
                        if (rtZipTarFssWrite_IsTar(pThis))
                        {
                            rc = rtZipTarFssWriter_FormatOffset(pThis->aHdrsTar[0].Common.size, cbReadTotal);
                            if (RT_SUCCESS(rc))
                                rc = rtZipTarFssWriter_ChecksumHdr(&pThis->aHdrsTar[0]);
                        }
                        else
                        {
                            if (pObjInfo->cbObject == (uint32_t)pObjInfo->cbObject)
                                rtZipTarFssWriter_CpioFmtU32(pThis->Cpio.Hdr.AsciiNew.achFileSize, (uint32_t)pObjInfo->cbObject);
                            else
                                rc = VERR_FILE_TOO_BIG;
                        }

                        if (RT_SUCCESS(rc))
                        {
                            rc = RTVfsFileWriteAt(pThis->hVfsFile, offHdr, &pThis->abHdrs[0], pThis->cbHdr, NULL);
                            if (RT_SUCCESS(rc))
                                rc = RTVfsFileSeek(pThis->hVfsFile, offRestore, RTFILE_SEEK_BEGIN, NULL);
                        }
                    }
                    else
                        rc = (int)offRestore;
                }

                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;
            }
        }
        else
            rc = (int)offHdr;
        pThis->rcFatal = rc;
    }
    return rc;
}


/**
 * Adds a file to the stream.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the file.
 * @param   hVfsIos         The I/O stream of the file.
 * @param   fFlags          The RTVFSFSSTREAMOPS::pfnAdd flags.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 */
static int rtZipTarFssWriter_AddFile(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, RTVFSIOSTREAM hVfsIos,
                                     PCRTFSOBJINFO pObjInfo, const char *pszOwnerNm, const char *pszGroupNm)
{
    /*
     * Append the header.
     */
    int rc = rtZipTarFssWriter_ObjInfoToHdr(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm, UINT8_MAX);
    if (RT_SUCCESS(rc))
    {
        rc = RTVfsIoStrmWrite(pThis->hVfsIos, pThis->abHdrs, pThis->cHdrs * pThis->cbHdr, true /*fBlocking*/, NULL);
        if (RT_SUCCESS(rc))
        {
            pThis->cbWritten += pThis->cHdrs * pThis->cbHdr;

            /*
             * Copy the bytes.  Padding the last buffer to a multiple of the block size.
             */
            void    *pvBufFree;
            size_t   cbBuf;
            uint8_t *pbBuf = rtZipTarFssWriter_AllocBuf(pThis, &cbBuf, &pvBufFree, pObjInfo->cbObject);

            uint64_t cbLeft = pObjInfo->cbObject;
            size_t cbBlock = rtZipTarFssWrite_GetBlockSize(pThis);
            while (cbLeft > 0)
            {
                size_t cbRead = cbLeft > cbBuf ? cbBuf : (size_t)cbLeft;
                rc = RTVfsIoStrmRead(hVfsIos, pbBuf, cbRead, true /*fBlocking*/, NULL);
                if (RT_FAILURE(rc))
                    break;

                size_t cbToWrite = cbRead;
                if (cbRead & (cbBlock - 1))
                {
                    size_t cbToZero = cbBlock - (cbRead & (cbBlock - 1));
                    memset(&pbBuf[cbRead], 0, cbToZero);
                    cbToWrite += cbToZero;
                }

                rc = RTVfsIoStrmWrite(pThis->hVfsIos, pbBuf, cbToWrite, true /*fBlocking*/, NULL);
                if (RT_FAILURE(rc))
                    break;
                pThis->cbWritten += cbToWrite;
                cbLeft -= cbRead;
            }

            RTMemTmpFree(pvBufFree);

            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
        }
        pThis->rcFatal = rc;
    }
    return rc;
}


/**
 * Adds a symbolic link to the stream.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the object.
 * @param   hVfsSymlink     The symbolic link object to add.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 */
static int rtZipTarFssWriter_AddSymlink(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, RTVFSSYMLINK hVfsSymlink,
                                        PCRTFSOBJINFO pObjInfo,  const char *pszOwnerNm, const char *pszGroupNm)
{
    /*
     * Read the symlink target first and check that it's not too long.
     * Flip DOS slashes.
     */
    char szTarget[RTPATH_MAX];
    int rc = RTVfsSymlinkRead(hVfsSymlink, szTarget,  sizeof(szTarget));
    if (RT_SUCCESS(rc))
    {
#if RTPATH_STYLE != RTPATH_STR_F_STYLE_UNIX
        char *pszDosSlash = strchr(szTarget, '\\');
        while (pszDosSlash)
        {
            *pszDosSlash = '/';
            pszDosSlash = strchr(pszDosSlash + 1, '\\');
        }
#endif
        size_t cchTarget = strlen(szTarget);
        if (rtZipTarFssWrite_IsTar(pThis))
        {
            if (cchTarget < sizeof(pThis->aHdrsTar[0].Common.linkname))
            {
                /*
                 * Create a header, add the link target and push it out.
                 */
                rc = rtZipTarFssWriter_ObjInfoToHdrTar(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm, UINT8_MAX);
                if (RT_SUCCESS(rc))
                {
                    memcpy(pThis->aHdrsTar[0].Common.linkname, szTarget, cchTarget + 1);
                    rc = rtZipTarFssWriter_ChecksumHdr(&pThis->aHdrsTar[0]);
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTVfsIoStrmWrite(pThis->hVfsIos, pThis->abHdrs, pThis->cHdrs * pThis->cbHdr,
                                              true /*fBlocking*/, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            pThis->cbWritten += pThis->cHdrs * pThis->cbHdr;
                            return VINF_SUCCESS;
                        }
                        pThis->rcFatal = rc;
                    }
                }
            }
            else
            {
                /** @todo implement gnu and pax long name extensions. */
                rc = VERR_TAR_NAME_TOO_LONG;
            }
        }
        else
        {
            /* CPIO stores the target path as file data after the header, without any zero terminator. */
            rc = rtZipTarFssWriter_ObjInfoToHdrCpio(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm);
            if (RT_SUCCESS(rc))
            {
                rc = RTVfsIoStrmWrite(pThis->hVfsIos, pThis->abHdrs, pThis->cHdrs * pThis->cbHdr,
                                      true /*fBlocking*/, NULL);
                if (RT_SUCCESS(rc))
                {
                    rc = RTVfsIoStrmWrite(pThis->hVfsIos, szTarget, cchTarget, true /*fBlocking*/, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        /* Need to pad the entry to a 4 byte aligned boundary. */
                        size_t cbPad = RT_ALIGN_32(cchTarget, 4) - cchTarget;
                        if (cbPad)
                            rc = RTVfsIoStrmWrite(pThis->hVfsIos, &pThis->Cpio.abPad[0], cbPad, true /*fBlocking*/, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            pThis->cbWritten += pThis->cHdrs * pThis->cbHdr + cbPad;
                            return VINF_SUCCESS;
                        }
                    }
                }
                pThis->rcFatal = rc;
            }
        }
    }
    return rc;
}


/**
 * Adds a simple object to the stream.
 *
 * Simple objects only contains metadata, no actual data bits.  Directories,
 * devices, fifos, sockets and such.
 *
 * @returns IPRT status code.
 * @param   pThis           The TAR writer instance.
 * @param   pszPath         The path to the object.
 * @param   pObjInfo        The object information.
 * @param   pszOwnerNm      The owner name.
 * @param   pszGroupNm      The group name.
 */
static int rtZipTarFssWriter_AddSimpleObject(PRTZIPTARFSSTREAMWRITER pThis, const char *pszPath, PCRTFSOBJINFO pObjInfo,
                                             const char *pszOwnerNm, const char *pszGroupNm)
{
    int rc = rtZipTarFssWriter_ObjInfoToHdr(pThis, pszPath, pObjInfo, pszOwnerNm, pszGroupNm, UINT8_MAX);
    if (RT_SUCCESS(rc))
    {
        rc = RTVfsIoStrmWrite(pThis->hVfsIos, pThis->abHdrs, pThis->cHdrs * pThis->cbHdr, true /*fBlocking*/, NULL);
        if (RT_SUCCESS(rc))
        {
            pThis->cbWritten += pThis->cHdrs * pThis->cbHdr;
            return VINF_SUCCESS;
        }
        pThis->rcFatal = rc;
    }
    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipTarFssWriter_Close(void *pvThis)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)pvThis;

    rtZipTarFssWriter_CompleteCurrentPushFile(pThis);

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    if (pThis->hVfsFile != NIL_RTVFSFILE)
    {
        RTVfsFileRelease(pThis->hVfsFile);
        pThis->hVfsFile = NIL_RTVFSFILE;
    }

    if (pThis->pszOwner)
    {
        RTStrFree(pThis->pszOwner);
        pThis->pszOwner = NULL;
    }
    if (pThis->pszGroup)
    {
        RTStrFree(pThis->pszGroup);
        pThis->pszGroup = NULL;
    }
    if (pThis->pszPrefix)
    {
        RTStrFree(pThis->pszPrefix);
        pThis->pszPrefix = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipTarFssWriter_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)pvThis;
    /* Take the lazy approach here, with the sideffect of providing some info
       that is actually kind of useful. */
    return RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnNext}
 */
static DECLCALLBACK(int) rtZipTarFssWriter_Next(void *pvThis, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)pvThis;

    /*
     * This only works in update mode and up to the point where
     * modifications takes place (truncating the archive or appending files).
     */
    AssertReturn(pThis->pReadTar || pThis->pReadCpio, VERR_ACCESS_DENIED);
    AssertReturn(pThis->fFlags & RTZIPTAR_C_UPDATE, VERR_ACCESS_DENIED);

    AssertReturn(!pThis->fWriting, VERR_WRONG_ORDER);

    if (rtZipTarFssWrite_IsTar(pThis))
        return rtZipTarFss_Next(pThis->pReadTar, ppszName, penmType, phVfsObj);
    else
        return rtZipCpioFss_Next(pThis->pReadCpio, ppszName, penmType, phVfsObj);
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnAdd}
 */
static DECLCALLBACK(int) rtZipTarFssWriter_Add(void *pvThis, const char *pszPath, RTVFSOBJ hVfsObj, uint32_t fFlags)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)pvThis;

    /*
     * Before we continue we must complete any current push file and check rcFatal.
     */
    int rc = rtZipTarFssWriter_CompleteCurrentPushFile(pThis);
    AssertRCReturn(rc, rc);

    /*
     * Query information about the object.
     */
    RTFSOBJINFO ObjInfo;
    rc = RTVfsObjQueryInfo(hVfsObj, &ObjInfo, RTFSOBJATTRADD_UNIX);
    AssertRCReturn(rc, rc);

    RTFSOBJINFO ObjOwnerName;
    rc = RTVfsObjQueryInfo(hVfsObj, &ObjOwnerName, RTFSOBJATTRADD_UNIX_OWNER);
    if (RT_FAILURE(rc) || ObjOwnerName.Attr.u.UnixOwner.szName[0] == '\0')
        strcpy(ObjOwnerName.Attr.u.UnixOwner.szName, "someone");

    RTFSOBJINFO ObjGrpName;
    rc = RTVfsObjQueryInfo(hVfsObj, &ObjGrpName, RTFSOBJATTRADD_UNIX_GROUP);
    if (RT_FAILURE(rc) || ObjGrpName.Attr.u.UnixGroup.szName[0] == '\0')
        strcpy(ObjGrpName.Attr.u.UnixGroup.szName, "somegroup");

    /*
     * Switch the stream into write mode if necessary.
     */
    rc = rtZipTarFssWriter_SwitchToWriteMode(pThis);
    AssertRCReturn(rc, rc);

    /*
     * Do type specific handling.  File have several options and variations to
     * take into account, thus the mess.
     */
    if (RTFS_IS_FILE(ObjInfo.Attr.fMode))
    {
        RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
        AssertReturn(hVfsIos != NIL_RTVFSIOSTREAM, VERR_WRONG_TYPE);

        if (fFlags & RTVFSFSSTRM_ADD_F_STREAM)
            rc = rtZipTarFssWriter_AddFileStream(pThis, pszPath, hVfsIos, &ObjInfo,
                                                 ObjOwnerName.Attr.u.UnixOwner.szName, ObjGrpName.Attr.u.UnixOwner.szName);
        else if (   !(pThis->fFlags & RTZIPTAR_C_SPARSE)
                 || ObjInfo.cbObject < RTZIPTAR_MIN_SPARSE
                 || !rtZipTarFssWrite_IsTar(pThis))
            rc = rtZipTarFssWriter_AddFile(pThis, pszPath, hVfsIos, &ObjInfo,
                                           ObjOwnerName.Attr.u.UnixOwner.szName, ObjGrpName.Attr.u.UnixOwner.szName);
        else
        {
            RTVFSFILE hVfsFile = RTVfsObjToFile(hVfsObj);
            if (hVfsFile != NIL_RTVFSFILE)
            {
                rc = rtZipTarFssWriter_AddFileSparse(pThis, pszPath, hVfsFile, hVfsIos, &ObjInfo,
                                                     ObjOwnerName.Attr.u.UnixOwner.szName, ObjGrpName.Attr.u.UnixOwner.szName);
                RTVfsFileRelease(hVfsFile);
            }
            else
                rc = rtZipTarFssWriter_AddFile(pThis, pszPath, hVfsIos, &ObjInfo,
                                               ObjOwnerName.Attr.u.UnixOwner.szName, ObjGrpName.Attr.u.UnixOwner.szName);
        }
        RTVfsIoStrmRelease(hVfsIos);
    }
    else if (RTFS_IS_SYMLINK(ObjInfo.Attr.fMode))
    {
        RTVFSSYMLINK hVfsSymlink = RTVfsObjToSymlink(hVfsObj);
        AssertReturn(hVfsSymlink != NIL_RTVFSSYMLINK, VERR_WRONG_TYPE);
        rc = rtZipTarFssWriter_AddSymlink(pThis, pszPath, hVfsSymlink, &ObjInfo,
                                          ObjOwnerName.Attr.u.UnixOwner.szName, ObjGrpName.Attr.u.UnixOwner.szName);
        RTVfsSymlinkRelease(hVfsSymlink);
    }
    else
        rc = rtZipTarFssWriter_AddSimpleObject(pThis, pszPath, &ObjInfo,
                                               ObjOwnerName.Attr.u.UnixOwner.szName, ObjGrpName.Attr.u.UnixOwner.szName);

    return rc;
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnPushFile}
 */
static DECLCALLBACK(int) rtZipTarFssWriter_PushFile(void *pvThis, const char *pszPath, uint64_t cbFile, PCRTFSOBJINFO paObjInfo,
                                                    uint32_t cObjInfo, uint32_t fFlags, PRTVFSIOSTREAM phVfsIos)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)pvThis;

    /*
     * We can only deal with output of indeterminate length if the output is
     * seekable (see also rtZipTarFssWriter_AddFileStream).
     */
    AssertReturn(cbFile != UINT64_MAX || pThis->hVfsFile != NIL_RTVFSFILE, VERR_NOT_A_FILE);
    AssertReturn(RT_BOOL(cbFile == UINT64_MAX) == RT_BOOL(fFlags & RTVFSFSSTRM_ADD_F_STREAM), VERR_INVALID_FLAGS);

    /*
     * Before we continue we must complete any current push file and check rcFatal.
     */
    int rc = rtZipTarFssWriter_CompleteCurrentPushFile(pThis);
    AssertRCReturn(rc, rc);

    /*
     * If no object info was provideded, fake up some.
     */
    const char *pszOwnerNm = "someone";
    const char *pszGroupNm = "somegroup";
    const size_t cbBlock = rtZipTarFssWrite_GetBlockSize(pThis);
    RTFSOBJINFO ObjInfo;
    if (cObjInfo == 0)
    {
        /* Fake up a info. */
        RT_ZERO(ObjInfo);
        ObjInfo.cbObject                    = cbFile != UINT64_MAX ? cbFile : 0;
        ObjInfo.cbAllocated                 = cbFile != UINT64_MAX ? RT_ALIGN_64(cbFile, cbBlock) : UINT64_MAX;
        RTTimeNow(&ObjInfo.ModificationTime);
        ObjInfo.BirthTime                   = ObjInfo.ModificationTime;
        ObjInfo.ChangeTime                  = ObjInfo.ModificationTime;
        ObjInfo.AccessTime                  = ObjInfo.ModificationTime;
        ObjInfo.Attr.fMode                  = RTFS_TYPE_FILE | 0666;
        ObjInfo.Attr.enmAdditional          = RTFSOBJATTRADD_UNIX;
        ObjInfo.Attr.u.Unix.uid             = NIL_RTUID;
        ObjInfo.Attr.u.Unix.gid             = NIL_RTGID;
        ObjInfo.Attr.u.Unix.cHardlinks      = 1;
        //ObjInfo.Attr.u.Unix.INodeIdDevice   = 0;
        //ObjInfo.Attr.u.Unix.INodeId         = 0;
        //ObjInfo.Attr.u.Unix.fFlags          = 0;
        //ObjInfo.Attr.u.Unix.GenerationId    = 0;
        //ObjInfo.Attr.u.Unix.Device          = 0;
    }
    else
    {
        /* Make a copy of the object info and adjust the size, if necessary. */
        ObjInfo = paObjInfo[0];
        Assert(ObjInfo.Attr.enmAdditional == RTFSOBJATTRADD_UNIX);
        Assert(RTFS_IS_FILE(ObjInfo.Attr.fMode));
        if ((uint64_t)ObjInfo.cbObject != cbFile)
        {
            ObjInfo.cbObject    = cbFile != UINT64_MAX ? cbFile : 0;
            ObjInfo.cbAllocated = cbFile != UINT64_MAX ? RT_ALIGN_64(cbFile, cbBlock) : UINT64_MAX;
        }

        /* Lookup the group and user names. */
        for (uint32_t i = 0; i < cObjInfo; i++)
            if (   paObjInfo[i].Attr.enmAdditional == RTFSOBJATTRADD_UNIX_OWNER
                && paObjInfo[i].Attr.u.UnixOwner.szName[0] != '\0')
                pszOwnerNm = paObjInfo[i].Attr.u.UnixOwner.szName;
            else if (   paObjInfo[i].Attr.enmAdditional == RTFSOBJATTRADD_UNIX_GROUP
                     && paObjInfo[i].Attr.u.UnixGroup.szName[0] != '\0')
                pszGroupNm = paObjInfo[i].Attr.u.UnixGroup.szName;
    }

    /*
     * Switch the stream into write mode if necessary.
     */
    rc = rtZipTarFssWriter_SwitchToWriteMode(pThis);
    AssertRCReturn(rc, rc);

    /*
     * Create an I/O stream object for the caller to use.
     */
    RTFOFF const offHdr = RTVfsIoStrmTell(pThis->hVfsIos);
    AssertReturn(offHdr >= 0, (int)offHdr);

    PRTZIPTARFSSTREAMWRITERPUSH pPush;
    RTVFSIOSTREAM hVfsIos;
    if (pThis->hVfsFile == NIL_RTVFSFILE)
    {
        rc = RTVfsNewIoStream(&g_rtZipTarWriterIoStrmOps, sizeof(*pPush), RTFILE_O_WRITE, NIL_RTVFS, NIL_RTVFSLOCK,
                              &hVfsIos, (void **)&pPush);
        if (RT_FAILURE(rc))
            return rc;
    }
    else
    {
        RTVFSFILE hVfsFile;
        rc = RTVfsNewFile(&g_rtZipTarWriterFileOps, sizeof(*pPush), RTFILE_O_WRITE, NIL_RTVFS, NIL_RTVFSLOCK,
                          &hVfsFile, (void **)&pPush);
        if (RT_FAILURE(rc))
            return rc;
        hVfsIos = RTVfsFileToIoStream(hVfsFile);
        RTVfsFileRelease(hVfsFile);
    }
    pPush->pParent      = NULL;
    pPush->cbExpected   = cbFile;
    pPush->offHdr       = (uint64_t)offHdr;
    pPush->offData      = 0;
    pPush->offCurrent   = 0;
    pPush->cbCurrent    = 0;
    pPush->ObjInfo      = ObjInfo;
    pPush->fOpenEnded   = cbFile == UINT64_MAX;

    /*
     * Produce and write file headers.
     */
    rc = rtZipTarFssWriter_ObjInfoToHdr(pThis, pszPath, &ObjInfo, pszOwnerNm, pszGroupNm, RTZIPTAR_TF_NORMAL);
    if (RT_SUCCESS(rc))
    {
        size_t cbHdrs = pThis->cHdrs * pThis->cbHdr;
        rc = RTVfsIoStrmWrite(pThis->hVfsIos, pThis->abHdrs, cbHdrs, true /*fBlocking*/, NULL);
        if (RT_SUCCESS(rc))
        {
            pThis->cbWritten += cbHdrs;

            /*
             * Complete the object and return.
             */
            pPush->offData = pPush->offHdr + cbHdrs;
            if (cbFile == UINT64_MAX)
                pPush->cbExpected = (uint64_t)(RTFOFF_MAX - _4K) - pPush->offData;
            pPush->pParent = pThis;
            pThis->pPush   = pPush;

            *phVfsIos = hVfsIos;
            return VINF_SUCCESS;
        }
        pThis->rcFatal = rc;
    }

    RTVfsIoStrmRelease(hVfsIos);
    return rc;
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnEnd}
 */
static DECLCALLBACK(int) rtZipTarFssWriter_End(void *pvThis)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)pvThis;

    /*
     * Make sure to complete any pending push file and that rcFatal is fine.
     */
    int rc = rtZipTarFssWriter_CompleteCurrentPushFile(pThis);
    if (RT_SUCCESS(rc))
    {
        if (rtZipTarFssWrite_IsTar(pThis))
        {
            /*
             * There are supposed to be two zero headers at the end of the archive.
             * GNU tar may write more because of the way it does buffering,
             * libarchive OTOH writes exactly two.
             */
            rc = RTVfsIoStrmWrite(pThis->hVfsIos, g_abRTZero4K, RTZIPTAR_BLOCKSIZE * 2, true /*fBlocking*/, NULL);
            if (RT_SUCCESS(rc))
                pThis->cbWritten += RTZIPTAR_BLOCKSIZE * 2;
        }
        else
        {
            /* CPIO has a special trailer marker. */
            RTFSOBJINFO ObjInfoEos; RT_ZERO(ObjInfoEos);
            rc = rtZipTarFssWriter_ObjInfoToHdrCpio(pThis, CPIO_EOS_FILE_NAME, &ObjInfoEos, NULL, NULL);
            if (RT_SUCCESS(rc))
                rc = RTVfsIoStrmWrite(pThis->hVfsIos, pThis->abHdrs, pThis->cbHdr, true /*fBlocking*/, NULL);
            if (RT_SUCCESS(rc))
                pThis->cbWritten += pThis->cbHdr;
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Flush the output.
             */
            rc = RTVfsIoStrmFlush(pThis->hVfsIos);

            /*
             * If we're in update mode, set the end-of-file here to make sure
             * unwanted bytes are really discarded.
             */
            if (RT_SUCCESS(rc) && (pThis->fFlags & RTZIPTAR_C_UPDATE))
            {
                RTFOFF cbTarFile = RTVfsFileTell(pThis->hVfsFile);
                if (cbTarFile >= 0)
                    rc =  RTVfsFileSetSize(pThis->hVfsFile, (uint64_t)cbTarFile, RTVFSFILE_SIZE_F_NORMAL);
                else
                    rc = (int)cbTarFile;
            }

            /*
             * Success?
             */
            if (RT_SUCCESS(rc))
                return rc;
        }
        pThis->rcFatal = rc;
    }
    return rc;
}


/**
 * Tar filesystem stream operations.
 */
static const RTVFSFSSTREAMOPS g_rtZipTarFssOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_FS_STREAM,
        "TarFsStreamWriter",
        rtZipTarFssWriter_Close,
        rtZipTarFssWriter_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSFSSTREAMOPS_VERSION,
    0,
    rtZipTarFssWriter_Next,
    rtZipTarFssWriter_Add,
    rtZipTarFssWriter_PushFile,
    rtZipTarFssWriter_End,
    RTVFSFSSTREAMOPS_VERSION
};


RTDECL(int) RTZipTarFsStreamToIoStream(RTVFSIOSTREAM hVfsIosOut, RTZIPTARFORMAT enmFormat,
                                       uint32_t fFlags, PRTVFSFSSTREAM phVfsFss)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(phVfsFss, VERR_INVALID_HANDLE);
    *phVfsFss = NIL_RTVFSFSSTREAM;
    AssertPtrReturn(hVfsIosOut, VERR_INVALID_HANDLE);
    AssertReturn(enmFormat > RTZIPTARFORMAT_INVALID && enmFormat < RTZIPTARFORMAT_END, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTZIPTAR_C_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!(fFlags & RTZIPTAR_C_UPDATE), VERR_NOT_SUPPORTED); /* Must use RTZipTarFsStreamForFile! */

    if (enmFormat == RTZIPTARFORMAT_DEFAULT)
        enmFormat = RTZIPTARFORMAT_GNU;
    AssertReturn(   enmFormat == RTZIPTARFORMAT_GNU
                 || enmFormat == RTZIPTARFORMAT_USTAR
                 || enmFormat == RTZIPTARFORMAT_CPIO_ASCII_NEW
                 , VERR_NOT_IMPLEMENTED); /* Only implementing GNU and USTAR output at the moment. */

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIosOut);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Retain the input stream and create a new filesystem stream handle.
     */
    PRTZIPTARFSSTREAMWRITER pThis;
    RTVFSFSSTREAM           hVfsFss;
    int rc = RTVfsNewFsStream(&g_rtZipTarFssOps, sizeof(*pThis), NIL_RTVFS, NIL_RTVFSLOCK, RTFILE_O_WRITE,
                              &hVfsFss, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsIos          = hVfsIosOut;
        pThis->hVfsFile         = RTVfsIoStrmToFile(hVfsIosOut);

        pThis->enmFormat        = enmFormat;
        pThis->fFlags           = fFlags;
        pThis->rcFatal          = VINF_SUCCESS;

        pThis->uidOwner         = NIL_RTUID;
        pThis->pszOwner         = NULL;
        pThis->gidGroup         = NIL_RTGID;
        pThis->pszGroup         = NULL;
        pThis->pszPrefix        = NULL;
        pThis->pModTime         = NULL;
        pThis->fFileModeAndMask = ~(RTFMODE)0;
        pThis->fFileModeOrMask  = 0;
        pThis->fDirModeAndMask  = ~(RTFMODE)0;
        pThis->fDirModeOrMask   = 0;
        pThis->fWriting         = true;

        *phVfsFss = hVfsFss;
        return VINF_SUCCESS;
    }

    RTVfsIoStrmRelease(hVfsIosOut);
    return rc;
}


RTDECL(int) RTZipTarFsStreamForFile(RTVFSFILE hVfsFile, RTZIPTARFORMAT enmFormat, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(phVfsFss, VERR_INVALID_HANDLE);
    *phVfsFss = NIL_RTVFSFSSTREAM;
    AssertReturn(hVfsFile != NIL_RTVFSFILE, VERR_INVALID_HANDLE);
    AssertReturn(enmFormat > RTZIPTARFORMAT_INVALID && enmFormat < RTZIPTARFORMAT_END, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTZIPTAR_C_VALID_MASK), VERR_INVALID_FLAGS);

    if (enmFormat == RTZIPTARFORMAT_DEFAULT)
        enmFormat = RTZIPTARFORMAT_GNU;
    AssertReturn(   enmFormat == RTZIPTARFORMAT_GNU
                 || enmFormat == RTZIPTARFORMAT_USTAR
                 || enmFormat == RTZIPTARFORMAT_CPIO_ASCII_NEW
                 , VERR_NOT_IMPLEMENTED); /* Only implementing GNU, USTAR and CPIO output at the moment. */

    RTFOFF const offStart = RTVfsFileTell(hVfsFile);
    AssertReturn(offStart >= 0, (int)offStart);

    uint32_t cRefs = RTVfsFileRetain(hVfsFile);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
    AssertReturnStmt(hVfsIos != NIL_RTVFSIOSTREAM, RTVfsFileRelease(hVfsFile), VERR_INVALID_HANDLE);

    /*
     * Retain the input stream and create a new filesystem stream handle.
     */
    PRTZIPTARFSSTREAMWRITER pThis;
    size_t const            cbThis = sizeof(*pThis) + (  fFlags & RTZIPTAR_C_UPDATE
                                                       ?   enmFormat == RTZIPTARFORMAT_CPIO_ASCII_NEW
                                                         ? sizeof(*pThis->pReadTar)
                                                         : sizeof(*pThis->pReadCpio)
                                                       : 0);
    RTVFSFSSTREAM           hVfsFss;
    int rc = RTVfsNewFsStream(&g_rtZipTarFssOps, cbThis, NIL_RTVFS, NIL_RTVFSLOCK,
                              fFlags & RTZIPTAR_C_UPDATE ? RTFILE_O_READWRITE : RTFILE_O_WRITE,
                              &hVfsFss, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsIos          = hVfsIos;
        pThis->hVfsFile         = hVfsFile;

        pThis->enmFormat        = enmFormat;
        pThis->fFlags           = fFlags;
        pThis->rcFatal          = VINF_SUCCESS;

        pThis->uidOwner         = NIL_RTUID;
        pThis->pszOwner         = NULL;
        pThis->gidGroup         = NIL_RTGID;
        pThis->pszGroup         = NULL;
        pThis->pszPrefix        = NULL;
        pThis->pModTime         = NULL;
        pThis->fFileModeAndMask = ~(RTFMODE)0;
        pThis->fFileModeOrMask  = 0;
        pThis->fDirModeAndMask  = ~(RTFMODE)0;
        pThis->fDirModeOrMask   = 0;
        if (!(fFlags & RTZIPTAR_C_UPDATE))
            pThis->fWriting     = true;
        else
        {
            pThis->fWriting     = false;
            if (rtZipTarFssWrite_IsTar(pThis))
            {
                pThis->pReadTar     = (PRTZIPTARFSSTREAM)(pThis + 1);
                rtZipTarReaderInit(pThis->pReadTar, hVfsIos, (uint64_t)offStart);
            }
            else
            {
                pThis->pReadCpio    = (PRTZIPCPIOFSSTREAM)(pThis + 1);
                rtZipCpioReaderInit(pThis->pReadCpio, hVfsIos, (uint64_t)offStart);
            }
        }

        *phVfsFss = hVfsFss;
        return VINF_SUCCESS;
    }

    RTVfsIoStrmRelease(hVfsIos);
    RTVfsFileRelease(hVfsFile);
    return rc;
}


RTDECL(int) RTZipTarFsStreamSetOwner(RTVFSFSSTREAM hVfsFss, RTUID uid, const char *pszOwner)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)RTVfsFsStreamToPrivate(hVfsFss, &g_rtZipTarFssOps);
    AssertReturn(pThis, VERR_WRONG_TYPE);

    pThis->uidOwner = uid;
    if (pThis->pszOwner)
    {
        RTStrFree(pThis->pszOwner);
        pThis->pszOwner = NULL;
    }
    if (pszOwner)
    {
        pThis->pszOwner = RTStrDup(pszOwner);
        AssertReturn(pThis->pszOwner, VERR_NO_STR_MEMORY);
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTZipTarFsStreamSetGroup(RTVFSFSSTREAM hVfsFss, RTGID gid, const char *pszGroup)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)RTVfsFsStreamToPrivate(hVfsFss, &g_rtZipTarFssOps);
    AssertReturn(pThis, VERR_WRONG_TYPE);

    pThis->gidGroup = gid;
    if (pThis->pszGroup)
    {
        RTStrFree(pThis->pszGroup);
        pThis->pszGroup = NULL;
    }
    if (pszGroup)
    {
        pThis->pszGroup = RTStrDup(pszGroup);
        AssertReturn(pThis->pszGroup, VERR_NO_STR_MEMORY);
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTZipTarFsStreamSetPrefix(RTVFSFSSTREAM hVfsFss, const char *pszPrefix)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)RTVfsFsStreamToPrivate(hVfsFss, &g_rtZipTarFssOps);
    AssertReturn(pThis, VERR_WRONG_TYPE);
    AssertReturn(!pszPrefix || *pszPrefix, VERR_INVALID_NAME);

    if (pThis->pszPrefix)
    {
        RTStrFree(pThis->pszPrefix);
        pThis->pszPrefix = NULL;
        pThis->cchPrefix = 0;
    }
    if (pszPrefix)
    {
        /*
         * Make a copy of the prefix, make sure it ends with a slash,
         * then flip DOS slashes.
         */
        size_t cchPrefix = strlen(pszPrefix);
        char *pszCopy = RTStrAlloc(cchPrefix + 3);
        AssertReturn(pszCopy, VERR_NO_STR_MEMORY);
        memcpy(pszCopy, pszPrefix, cchPrefix + 1);

        RTPathEnsureTrailingSeparator(pszCopy, cchPrefix + 3);

#if RTPATH_STYLE != RTPATH_STR_F_STYLE_UNIX
        char *pszDosSlash = strchr(pszCopy, '\\');
        while (pszDosSlash)
        {
            *pszDosSlash = '/';
            pszDosSlash = strchr(pszDosSlash + 1, '\\');
        }
#endif

        pThis->cchPrefix = cchPrefix + strlen(&pszCopy[cchPrefix]);
        pThis->pszPrefix = pszCopy;
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTZipTarFsStreamSetModTime(RTVFSFSSTREAM hVfsFss, PCRTTIMESPEC pModificationTime)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)RTVfsFsStreamToPrivate(hVfsFss, &g_rtZipTarFssOps);
    AssertReturn(pThis, VERR_WRONG_TYPE);

    if (pModificationTime)
    {
        pThis->ModTime  = *pModificationTime;
        pThis->pModTime = &pThis->ModTime;
    }
    else
        pThis->pModTime = NULL;

    return VINF_SUCCESS;
}


RTDECL(int) RTZipTarFsStreamSetFileMode(RTVFSFSSTREAM hVfsFss, RTFMODE fAndMode, RTFMODE fOrMode)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)RTVfsFsStreamToPrivate(hVfsFss, &g_rtZipTarFssOps);
    AssertReturn(pThis, VERR_WRONG_TYPE);

    pThis->fFileModeAndMask = fAndMode | ~RTFS_UNIX_ALL_PERMS;
    pThis->fFileModeOrMask  = fOrMode  & RTFS_UNIX_ALL_PERMS;
    return VINF_SUCCESS;
}


RTDECL(int) RTZipTarFsStreamSetDirMode(RTVFSFSSTREAM hVfsFss, RTFMODE fAndMode, RTFMODE fOrMode)
{
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)RTVfsFsStreamToPrivate(hVfsFss, &g_rtZipTarFssOps);
    AssertReturn(pThis, VERR_WRONG_TYPE);

    pThis->fDirModeAndMask = fAndMode | ~RTFS_UNIX_ALL_PERMS;
    pThis->fDirModeOrMask  = fOrMode  & RTFS_UNIX_ALL_PERMS;
    return VINF_SUCCESS;
}


RTDECL(int) RTZipTarFsStreamTruncate(RTVFSFSSTREAM hVfsFss, RTVFSOBJ hVfsObj, bool fAfter)
{
    /*
     * Translate and validate the input.
     */
    PRTZIPTARFSSTREAMWRITER pThis = (PRTZIPTARFSSTREAMWRITER)RTVfsFsStreamToPrivate(hVfsFss, &g_rtZipTarFssOps);
    AssertReturn(pThis, VERR_WRONG_TYPE);

    AssertReturn(hVfsObj != NIL_RTVFSOBJ, VERR_INVALID_HANDLE);
    AssertReturn(pThis->pReadTar || pThis->pReadCpio, VERR_ACCESS_DENIED);
    AssertReturn(pThis->fFlags & RTZIPTAR_C_UPDATE, VERR_ACCESS_DENIED);
    AssertReturn(!pThis->fWriting, VERR_WRONG_ORDER);

    int rc;
    if (rtZipTarFssWrite_IsTar(pThis))
    {
        PRTZIPTARBASEOBJ pThisObj = rtZipTarFsStreamBaseObjToPrivate(pThis->pReadTar, hVfsObj);
        AssertReturn(pThisObj, VERR_NOT_OWNER);

        /*
         * Seek to the desired cut-off point and indicate that we've switched to writing.
         */
        rc = RTVfsFileSeek(pThis->hVfsFile, fAfter ? pThisObj->offNextHdr : pThisObj->offHdr,
                           RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
    }
    else
    {
        PRTZIPCPIOBASEOBJ pThisObj = rtZipCpioFsStreamBaseObjToPrivate(pThis->pReadCpio, hVfsObj);
        AssertReturn(pThisObj, VERR_NOT_OWNER);

        /*
         * Seek to the desired cut-off point and indicate that we've switched to writing.
         */
        rc = RTVfsFileSeek(pThis->hVfsFile, fAfter ? pThisObj->offNextHdr : pThisObj->offHdr,
                           RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
    }
    if (RT_SUCCESS(rc))
        pThis->fWriting = true;
    else
        pThis->rcFatal = rc;
    return rc;
}

