/* $Id: xarvfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - XAR Virtual Filesystem.
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

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/md5.h>
#include <iprt/poll.h>
#include <iprt/file.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/formats/xar.h>
#include <iprt/cpp/xml.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name Hash state
 * @{ */
#define RTZIPXAR_HASH_PENDING           0
#define RTZIPXAR_HASH_OK                1
#define RTZIPXAR_HASH_FAILED_ARCHIVED   2
#define RTZIPXAR_HASH_FAILED_EXTRACTED  3
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Hash digest value union for the supported XAR hash functions.
 * @todo This could be generalized in iprt/checksum.h or somewhere.
 */
typedef union RTZIPXARHASHDIGEST
{
    uint8_t abMd5[RTMD5_HASH_SIZE];
    uint8_t abSha1[RTSHA1_HASH_SIZE];
} RTZIPXARHASHDIGEST;
/** Pointer to a XAR hash digest union. */
typedef RTZIPXARHASHDIGEST *PRTZIPXARHASHDIGEST;
/** Pointer to a const XAR hash digest union. */
typedef RTZIPXARHASHDIGEST *PCRTZIPXARHASHDIGEST;

/**
 * Hash context union.
 */
typedef union RTZIPXARHASHCTX
{
    RTMD5CONTEXT    Md5;
    RTSHA1CONTEXT   Sha1;
} RTZIPXARHASHCTX;
/** Pointer to a hash context union. */
typedef RTZIPXARHASHCTX *PRTZIPXARHASHCTX;

/**
 * XAR reader instance data.
 */
typedef struct RTZIPXARREADER
{
    /** The TOC XML element. */
    xml::ElementNode const *pToc;
    /** The TOC XML document. */
    xml::Document          *pDoc;

    /** The current file. */
    xml::ElementNode const *pCurFile;
    /** The depth of the current file, with 0 being the root level. */
    uint32_t                cCurDepth;
} RTZIPXARREADER;
/** Pointer to the XAR reader instance data. */
typedef RTZIPXARREADER *PRTZIPXARREADER;

/**
 * Xar directory, character device, block device, fifo socket or symbolic link.
 */
typedef struct RTZIPXARBASEOBJ
{
    /** The file TOC element. */
    xml::ElementNode const *pFileElem;
    /** RTFS_TYPE_XXX value for the object. */
    RTFMODE                 fModeType;
} RTZIPXARBASEOBJ;
/** Pointer to a XAR filesystem stream base object. */
typedef RTZIPXARBASEOBJ *PRTZIPXARBASEOBJ;


/**
 * XAR data encoding.
 */
typedef enum RTZIPXARENCODING
{
    RTZIPXARENCODING_INVALID = 0,
    RTZIPXARENCODING_STORE,
    RTZIPXARENCODING_GZIP,
    RTZIPXARENCODING_UNSUPPORTED,
    RTZIPXARENCODING_END
} RTZIPXARENCODING;


/**
 * Data stream attributes.
 */
typedef struct RTZIPXARDATASTREAM
{
    /** Offset of the data in the stream.
     * @remarks The I/O stream and file constructor will adjust this so that it
     *          relative to the start of the input stream, instead of the first byte
     *          after the TOC. */
    RTFOFF                  offData;
    /** The size of the archived data. */
    RTFOFF                  cbDataArchived;
    /** The size of the extracted data. */
    RTFOFF                  cbDataExtracted;
    /** The encoding of the archived ata. */
    RTZIPXARENCODING        enmEncoding;
    /** The hash function used for the archived data. */
    uint8_t                 uHashFunArchived;
    /** The hash function used for the extracted data. */
    uint8_t                 uHashFunExtracted;
    /** The digest of the archived data. */
    RTZIPXARHASHDIGEST      DigestArchived;
    /** The digest of the extracted data. */
    RTZIPXARHASHDIGEST      DigestExtracted;
} RTZIPXARDATASTREAM;
/** Pointer to XAR data stream attributes. */
typedef RTZIPXARDATASTREAM *PRTZIPXARDATASTREAM;


/**
 * Xar file represented as a VFS I/O stream.
 */
typedef struct RTZIPXARIOSTREAM
{
    /** The basic XAR object data. */
    RTZIPXARBASEOBJ         BaseObj;
    /** The attributes of the primary data stream. */
    RTZIPXARDATASTREAM      DataAttr;
    /** The current file position in the archived file. */
    RTFOFF                  offCurPos;
    /** The input I/O stream. */
    RTVFSIOSTREAM           hVfsIos;
    /** Set if we've reached the end of the file or if the next object in the
     * file system stream has been requested. */
    bool                    fEndOfStream;
    /** Whether the stream is seekable. */
    bool                    fSeekable;
    /** Hash state. */
    uint8_t                 uHashState;
    /** The size of the file that we've currently hashed.
     * We use this to check whether the user skips part of the file while reading
     * and when to compare the digests. */
    RTFOFF                  cbDigested;
    /** The digest of the archived data. */
    RTZIPXARHASHCTX         CtxArchived;
    /** The digest of the extracted data. */
    RTZIPXARHASHCTX         CtxExtracted;
} RTZIPXARIOSTREAM;
/** Pointer to a the private data of a XAR file I/O stream. */
typedef RTZIPXARIOSTREAM *PRTZIPXARIOSTREAM;


/**
 * Xar file represented as a VFS file.
 */
typedef struct RTZIPXARFILE
{
    /** The XAR I/O stream data. */
    RTZIPXARIOSTREAM        Ios;
    /** The input file. */
    RTVFSFILE               hVfsFile;
} RTZIPXARFILE;
/** Pointer to the private data of a XAR file. */
typedef RTZIPXARFILE *PRTZIPXARFILE;


/**
 * Decompressed I/O stream instance.
 *
 * This is just a front that checks digests and other sanity stuff.
 */
typedef struct RTZIPXARDECOMPIOS
{
    /** The decompressor I/O stream. */
    RTVFSIOSTREAM           hVfsIosDecompressor;
    /** The raw XAR I/O stream. */
    RTVFSIOSTREAM           hVfsIosRaw;
    /** Pointer to the raw XAR I/O stream instance data. */
    PRTZIPXARIOSTREAM       pIosRaw;
    /** The current file position in the archived file. */
    RTFOFF                  offCurPos;
    /** The hash function to use on the extracted data. */
    uint8_t                 uHashFunExtracted;
    /** Hash state on the extracted data. */
    uint8_t                 uHashState;
    /** The digest of the extracted data. */
    RTZIPXARHASHCTX         CtxExtracted;
    /** The expected digest of the extracted data. */
    RTZIPXARHASHDIGEST      DigestExtracted;
} RTZIPXARDECOMPIOS;
/** Pointer to the private data of a XAR decompressed I/O stream. */
typedef RTZIPXARDECOMPIOS *PRTZIPXARDECOMPIOS;


/**
 * Xar filesystem stream private data.
 */
typedef struct RTZIPXARFSSTREAM
{
    /** The input I/O stream. */
    RTVFSIOSTREAM           hVfsIos;
    /** The input file, if the stream is actually a file. */
    RTVFSFILE               hVfsFile;

    /** The start offset in the input I/O stream. */
    RTFOFF                  offStart;
    /** The zero offset in the file which all others are relative to. */
    RTFOFF                  offZero;

    /** The hash function we're using (XAR_HASH_XXX). */
    uint8_t                 uHashFunction;
    /** The size of the digest produced by the hash function we're using. */
    uint8_t                 cbHashDigest;

    /** Set if we've reached the end of the stream. */
    bool                    fEndOfStream;
    /** Set if we've encountered a fatal error. */
    int                     rcFatal;


    /** The XAR reader instance data. */
    RTZIPXARREADER          XarReader;
} RTZIPXARFSSTREAM;
/** Pointer to a the private data of a XAR filesystem stream. */
typedef RTZIPXARFSSTREAM *PRTZIPXARFSSTREAM;


/**
 * Hashes a block of data.
 *
 * @param   uHashFunction   The hash function to use.
 * @param   pvSrc           The data to hash.
 * @param   cbSrc           The size of the data to hash.
 * @param   pHashDigest     Where to return the message digest.
 */
static void rtZipXarCalcHash(uint32_t uHashFunction, void const *pvSrc, size_t cbSrc, PRTZIPXARHASHDIGEST pHashDigest)
{
    switch (uHashFunction)
    {
        case XAR_HASH_SHA1:
            RTSha1(pvSrc, cbSrc, pHashDigest->abSha1);
            break;
        case XAR_HASH_MD5:
            RTMd5(pvSrc, cbSrc, pHashDigest->abMd5);
            break;
        default:
            RT_ZERO(*pHashDigest);
            break;
    }
}


/**
 * Initializes a hash context.
 *
 * @param   pCtx            Pointer to the context union.
 * @param   uHashFunction   The hash function to use.
 */
static void rtZipXarHashInit(PRTZIPXARHASHCTX pCtx, uint32_t uHashFunction)
{
    switch (uHashFunction)
    {
        case XAR_HASH_SHA1:
            RTSha1Init(&pCtx->Sha1);
            break;
        case XAR_HASH_MD5:
            RTMd5Init(&pCtx->Md5);;
            break;
        default:
            RT_ZERO(*pCtx);
            break;
    }
}


/**
 * Adds a block to the hash calculation.
 *
 * @param   pCtx            Pointer to the context union.
 * @param   uHashFunction   The hash function to use.
 * @param   pvSrc           The data to add to the hash.
 * @param   cbSrc           The size of the data.
 */
static void rtZipXarHashUpdate(PRTZIPXARHASHCTX pCtx, uint32_t uHashFunction, void const *pvSrc, size_t cbSrc)
{
    switch (uHashFunction)
    {
        case XAR_HASH_SHA1:
            RTSha1Update(&pCtx->Sha1, pvSrc, cbSrc);
            break;
        case XAR_HASH_MD5:
            RTMd5Update(&pCtx->Md5, pvSrc, cbSrc);
            break;
    }
}


/**
 * Finalizes the hash, producing the message digest.
 *
 * @param   pCtx            Pointer to the context union.
 * @param   uHashFunction   The hash function to use.
 * @param   pHashDigest     Where to return the message digest.
 */
static void rtZipXarHashFinal(PRTZIPXARHASHCTX pCtx, uint32_t uHashFunction, PRTZIPXARHASHDIGEST pHashDigest)
{
    switch (uHashFunction)
    {
        case XAR_HASH_SHA1:
            RTSha1Final(&pCtx->Sha1, pHashDigest->abSha1);
            break;
        case XAR_HASH_MD5:
            RTMd5Final(pHashDigest->abMd5, &pCtx->Md5);
            break;
        default:
            RT_ZERO(*pHashDigest);
            break;
    }
}


/**
 * Compares two hash digests.
 *
 * @returns true if equal, false if not.
 * @param   uHashFunction   The hash function to use.
 * @param   pHashDigest1    The first hash digest.
 * @param   pHashDigest2    The second hash digest.
 */
static bool rtZipXarHashIsEqual(uint32_t uHashFunction, PRTZIPXARHASHDIGEST pHashDigest1, PRTZIPXARHASHDIGEST pHashDigest2)
{
    switch (uHashFunction)
    {
        case XAR_HASH_SHA1:
            return memcmp(pHashDigest1->abSha1, pHashDigest2->abSha1, sizeof(pHashDigest1->abSha1)) == 0;
        case XAR_HASH_MD5:
            return memcmp(pHashDigest1->abMd5, pHashDigest2->abMd5, sizeof(pHashDigest1->abMd5)) == 0;
        default:
            return true;
    }
}


/**
 * Gets the 'offset', 'size' and optionally 'length' sub elements.
 *
 * @returns IPRT status code.
 * @param   pElement            The parent element.
 * @param   poff                Where to return the offset value.
 * @param   pcbSize             Where to return the size value.
 * @param   pcbLength           Where to return the length value, optional.
 */
static int rtZipXarGetOffsetSizeLengthFromElem(xml::ElementNode const *pElement,
                                               PRTFOFF poff, PRTFOFF pcbSize, PRTFOFF pcbLength)
{
    /*
     * The offset.
     */
    xml::ElementNode const *pElem = pElement->findChildElement("offset");
    if (!pElem)
        return VERR_XAR_MISSING_OFFSET_ELEMENT;
    const char *pszValue = pElem->getValue();
    if (!pszValue)
        return VERR_XAR_BAD_OFFSET_ELEMENT;

    int rc = RTStrToInt64Full(pszValue, 0, poff);
    if (   RT_FAILURE(rc)
        || rc == VWRN_NUMBER_TOO_BIG
        || *poff > RTFOFF_MAX / 2 /* make sure to not overflow calculating offsets. */
        || *poff < 0)
        return VERR_XAR_BAD_OFFSET_ELEMENT;

    /*
     * The 'size' stored in the archive.
     */
    pElem = pElement->findChildElement("size");
    if (!pElem)
        return VERR_XAR_MISSING_SIZE_ELEMENT;

    pszValue = pElem->getValue();
    if (!pszValue)
        return VERR_XAR_BAD_SIZE_ELEMENT;

    rc = RTStrToInt64Full(pszValue, 0, pcbSize);
    if (   RT_FAILURE(rc)
        || rc == VWRN_NUMBER_TOO_BIG
        || *pcbSize >= RTFOFF_MAX - _1M
        || *pcbSize < 0)
        return VERR_XAR_BAD_SIZE_ELEMENT;
    AssertCompile(RTFOFF_MAX == UINT64_MAX / 2);

    /*
     * The 'length' of the uncompressed data.  Not present for checksums, so
     * the caller might not want it.
     */
    if (pcbLength)
    {
        pElem = pElement->findChildElement("length");
        if (!pElem)
            return VERR_XAR_MISSING_LENGTH_ELEMENT;

        pszValue = pElem->getValue();
        if (!pszValue)
            return VERR_XAR_BAD_LENGTH_ELEMENT;

        rc = RTStrToInt64Full(pszValue, 0, pcbLength);
        if (   RT_FAILURE(rc)
            || rc == VWRN_NUMBER_TOO_BIG
            || *pcbLength >= RTFOFF_MAX - _1M
            || *pcbLength < 0)
            return VERR_XAR_BAD_LENGTH_ELEMENT;
        AssertCompile(RTFOFF_MAX == UINT64_MAX / 2);
    }

    return VINF_SUCCESS;
}


/**
 * Convers a checksum style value into a XAR hash function number.
 *
 * @returns IPRT status code.
 * @param   pszStyle        The XAR checksum style.
 * @param   puHashFunction  Where to return the hash function number on success.
 */
static int rtZipXarParseChecksumStyle(const char *pszStyle, uint8_t *puHashFunction)
{
    size_t cchStyle = strlen(pszStyle);
    if (   cchStyle == 4
        && (pszStyle[0] == 's' || pszStyle[0] == 'S')
        && (pszStyle[1] == 'h' || pszStyle[1] == 'H')
        && (pszStyle[2] == 'a' || pszStyle[2] == 'A')
        &&  pszStyle[3] == '1' )
        *puHashFunction = XAR_HASH_SHA1;
    else if (   cchStyle == 3
             && (pszStyle[0] == 'm' || pszStyle[0] == 'M')
             && (pszStyle[1] == 'd' || pszStyle[1] == 'D')
             &&  pszStyle[2] == '5' )
        *puHashFunction = XAR_HASH_MD5;
    else if (   cchStyle == 4
             && (pszStyle[0] == 'n' || pszStyle[0] == 'N')
             && (pszStyle[1] == 'o' || pszStyle[1] == 'O')
             && (pszStyle[2] == 'n' || pszStyle[2] == 'N')
             && (pszStyle[3] == 'e' || pszStyle[3] == 'E') )
        *puHashFunction = XAR_HASH_NONE;
    else
    {
        *puHashFunction = UINT8_MAX;
        return VERR_XAR_BAD_CHECKSUM_ELEMENT;
    }
    return VINF_SUCCESS;
}


/**
 * Parses a checksum element typically found under 'data'.
 *
 * @returns IPRT status code.
 * @param   pParentElem     The parent element ('data').
 * @param   pszName         The name of the element, like 'checksum-archived' or
 *                          'checksum-extracted'.
 * @param   puHashFunction  Where to return the XAR hash function number.
 * @param   pDigest         Where to return the expected message digest.
 */
static int rtZipXarParseChecksumElem(xml::ElementNode const *pParentElem, const char *pszName,
                                     uint8_t *puHashFunction, PRTZIPXARHASHDIGEST pDigest)
{
    /* Default is no checksum. */
    *puHashFunction = XAR_HASH_NONE;
    RT_ZERO(*pDigest);

    xml::ElementNode const *pChecksumElem = pParentElem->findChildElement(pszName);
    if (!pChecksumElem)
        return VINF_SUCCESS;

    /* The style. */
    const char *pszStyle = pChecksumElem->findAttributeValue("style");
    if (!pszStyle)
        return VERR_XAR_BAD_CHECKSUM_ELEMENT;
    int rc = rtZipXarParseChecksumStyle(pszStyle, puHashFunction);
    if (RT_FAILURE(rc))
        return rc;

    if (*puHashFunction == XAR_HASH_NONE)
        return VINF_SUCCESS;

    /* The digest. */
    const char *pszDigest = pChecksumElem->getValue();
    if (!pszDigest)
        return VERR_XAR_BAD_CHECKSUM_ELEMENT;

    switch (*puHashFunction)
    {
        case XAR_HASH_SHA1:
            rc = RTSha1FromString(pszDigest, pDigest->abSha1);
            break;
        case XAR_HASH_MD5:
            rc = RTMd5FromString(pszDigest, pDigest->abMd5);
            break;
        default:
            rc = VERR_INTERNAL_ERROR_2;
    }
    return rc;
}


/**
 * Gets all the attributes of the primary data stream.
 *
 * @returns IPRT status code.
 * @param   pFileElem           The file element, we'll be parsing the 'data'
 *                              sub element of this.
 * @param   pDataAttr           Where to return the attributes.
 */
static int rtZipXarGetDataStreamAttributes(xml::ElementNode const *pFileElem, PRTZIPXARDATASTREAM pDataAttr)
{
    /*
     * Get the data element.
     */
    xml::ElementNode const *pDataElem = pFileElem->findChildElement("data");
    if (!pDataElem)
        return VERR_XAR_MISSING_DATA_ELEMENT;

    /*
     * Checksums.
     */
    int rc = rtZipXarParseChecksumElem(pDataElem, "extracted-checksum",
                                       &pDataAttr->uHashFunExtracted, &pDataAttr->DigestExtracted);
    if (RT_FAILURE(rc))
        return rc;
    rc = rtZipXarParseChecksumElem(pDataElem, "archived-checksum",
                                   &pDataAttr->uHashFunArchived, &pDataAttr->DigestArchived);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * The encoding.
     */
    const char *pszEncoding = pDataElem->findChildElementAttributeValueP("encoding", "style");
    if (!pszEncoding)
        return VERR_XAR_NO_ENCODING;
    if (!strcmp(pszEncoding, "application/octet-stream"))
        pDataAttr->enmEncoding = RTZIPXARENCODING_STORE;
    else if (!strcmp(pszEncoding, "application/x-gzip"))
        pDataAttr->enmEncoding = RTZIPXARENCODING_GZIP;
    else
        pDataAttr->enmEncoding = RTZIPXARENCODING_UNSUPPORTED;

    /*
     * The data offset and the compressed and uncompressed sizes.
     */
    rc = rtZipXarGetOffsetSizeLengthFromElem(pDataElem, &pDataAttr->offData,
                                             &pDataAttr->cbDataExtracted, &pDataAttr->cbDataArchived);
    if (RT_FAILURE(rc))
        return rc;

    /* No zero padding or other alignment crap, please. */
    if (   pDataAttr->enmEncoding == RTZIPXARENCODING_STORE
        && pDataAttr->cbDataExtracted != pDataAttr->cbDataArchived)
        return VERR_XAR_ARCHIVED_AND_EXTRACTED_SIZES_MISMATCH;

    return VINF_SUCCESS;
}


/**
 * Parses a timestamp.
 *
 * We consider all timestamps optional, and will only fail (return @c false) on
 * parse errors.  If the specified element isn't found, we'll return epoc time.
 *
 * @returns boolean success indicator.
 * @param   pParent             The parent element (typically 'file').
 * @param   pszChild            The name of the child element.
 * @param   pTimeSpec           Where to return the timespec on success.
 */
static bool rtZipXarParseTimestamp(const xml::ElementNode *pParent, const char *pszChild, PRTTIMESPEC pTimeSpec)
{
    const char *pszValue = pParent->findChildElementValueP(pszChild);
    if (pszValue)
    {
        if (RTTimeSpecFromString(pTimeSpec, pszValue))
            return true;
        return false;
    }
    RTTimeSpecSetNano(pTimeSpec, 0);
    return true;
}


/**
 * Gets the next file element in the TOC.
 *
 * @returns Pointer to the next file, NULL if we've reached the end.
 * @param   pCurFile            The current element.
 * @param   pcCurDepth          Depth gauge we update when decending and
 *                              acending thru the tree.
 */
static xml::ElementNode const *rtZipXarGetNextFileElement(xml::ElementNode const *pCurFile, uint32_t *pcCurDepth)
{
    /*
     * Consider children first.
     */
    xml::ElementNode const *pChild = pCurFile->findChildElement("file");
    if (pChild)
    {
        *pcCurDepth += 1;
        return pChild;
    }

    /*
     * Siblings and ancestor siblings.
     */
    for (;;)
    {
        xml::ElementNode const *pSibling = pCurFile->findNextSibilingElement("file");
        if (pSibling != NULL)
            return pSibling;

        if (*pcCurDepth == 0)
            break;
        *pcCurDepth -= 1;
        pCurFile = static_cast<const xml::ElementNode *>(pCurFile->getParent());
        AssertBreak(pCurFile);
        Assert(pCurFile->nameEquals("file"));
    }

    return NULL;
}



/*
 *
 * T h e   V F S   F i l e s y s t e m   S t r e a m   B i t s.
 * T h e   V F S   F i l e s y s t e m   S t r e a m   B i t s.
 * T h e   V F S   F i l e s y s t e m   S t r e a m   B i t s.
 *
 */


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipXarFssBaseObj_Close(void *pvThis)
{
    PRTZIPXARBASEOBJ pThis = (PRTZIPXARBASEOBJ)pvThis;

    /* Currently there is nothing we really have to do here. */
    NOREF(pThis);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipXarFssBaseObj_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPXARBASEOBJ pThis = (PRTZIPXARBASEOBJ)pvThis;

    /*
     * Get the common data.
     */

    /* Sizes. */
    if (pThis->fModeType == RTFS_TYPE_FILE)
    {
        PRTZIPXARIOSTREAM pThisIos = RT_FROM_MEMBER(pThis, RTZIPXARIOSTREAM, BaseObj);
        pObjInfo->cbObject    = pThisIos->DataAttr.cbDataArchived; /* Modified by decomp ios. */
        pObjInfo->cbAllocated = pThisIos->DataAttr.cbDataArchived;
    }
    else
    {
        pObjInfo->cbObject    = 0;
        pObjInfo->cbAllocated = 0;
    }

    /* The file mode. */
    if (RT_UNLIKELY(!pThis->pFileElem->getChildElementValueDefP("mode", 0755, &pObjInfo->Attr.fMode)))
        return VERR_XAR_BAD_FILE_MODE;
    if (pObjInfo->Attr.fMode & RTFS_TYPE_MASK)
        return VERR_XAR_BAD_FILE_MODE;
    pObjInfo->Attr.fMode &= RTFS_UNIX_MASK & ~RTFS_TYPE_MASK;
    pObjInfo->Attr.fMode |= pThis->fModeType;

    /* File times. */
    if (RT_UNLIKELY(!rtZipXarParseTimestamp(pThis->pFileElem, "atime", &pObjInfo->AccessTime)))
        return VERR_XAR_BAD_FILE_TIMESTAMP;
    if (RT_UNLIKELY(!rtZipXarParseTimestamp(pThis->pFileElem, "ctime", &pObjInfo->ChangeTime)))
        return VERR_XAR_BAD_FILE_TIMESTAMP;
    if (RT_UNLIKELY(!rtZipXarParseTimestamp(pThis->pFileElem, "mtime", &pObjInfo->ModificationTime)))
        return VERR_XAR_BAD_FILE_TIMESTAMP;
    pObjInfo->BirthTime = RTTimeSpecGetNano(&pObjInfo->AccessTime) <= RTTimeSpecGetNano(&pObjInfo->ChangeTime)
                        ? pObjInfo->AccessTime : pObjInfo->ChangeTime;
    if (RTTimeSpecGetNano(&pObjInfo->BirthTime) > RTTimeSpecGetNano(&pObjInfo->ModificationTime))
        pObjInfo->BirthTime = pObjInfo->ModificationTime;

    /*
     * Copy the desired data.
     */
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING:
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.enmAdditional = RTFSOBJATTRADD_UNIX;
            if (RT_UNLIKELY(!pThis->pFileElem->getChildElementValueDefP("uid", 0, &pObjInfo->Attr.u.Unix.uid)))
                return VERR_XAR_BAD_FILE_UID;
            if (RT_UNLIKELY(!pThis->pFileElem->getChildElementValueDefP("gid", 0, &pObjInfo->Attr.u.Unix.gid)))
                return VERR_XAR_BAD_FILE_GID;
            if (RT_UNLIKELY(!pThis->pFileElem->getChildElementValueDefP("deviceno", 0, &pObjInfo->Attr.u.Unix.INodeIdDevice)))
                return VERR_XAR_BAD_FILE_DEVICE_NO;
            if (RT_UNLIKELY(!pThis->pFileElem->getChildElementValueDefP("inode", 0, &pObjInfo->Attr.u.Unix.INodeId)))
                return VERR_XAR_BAD_FILE_INODE;
            pObjInfo->Attr.u.Unix.cHardlinks    = 1;
            pObjInfo->Attr.u.Unix.fFlags        = 0;
            pObjInfo->Attr.u.Unix.GenerationId  = 0;
            pObjInfo->Attr.u.Unix.Device        = 0;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
        {
            pObjInfo->Attr.enmAdditional = RTFSOBJATTRADD_UNIX_OWNER;
            if (RT_UNLIKELY(!pThis->pFileElem->getChildElementValueDefP("uid", 0, &pObjInfo->Attr.u.Unix.uid)))
                return VERR_XAR_BAD_FILE_UID;
            const char *pszUser = pThis->pFileElem->findChildElementValueP("user");
            if (pszUser)
                RTStrCopy(pObjInfo->Attr.u.UnixOwner.szName, sizeof(pObjInfo->Attr.u.UnixOwner.szName), pszUser);
            else
                pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            break;
        }

        case RTFSOBJATTRADD_UNIX_GROUP:
        {
            pObjInfo->Attr.enmAdditional = RTFSOBJATTRADD_UNIX_GROUP;
            if (RT_UNLIKELY(!pThis->pFileElem->getChildElementValueDefP("gid", 0, &pObjInfo->Attr.u.Unix.gid)))
                return VERR_XAR_BAD_FILE_GID;
            const char *pszGroup = pThis->pFileElem->findChildElementValueP("group");
            if (pszGroup)
                RTStrCopy(pObjInfo->Attr.u.UnixGroup.szName, sizeof(pObjInfo->Attr.u.UnixGroup.szName), pszGroup);
            else
                pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            break;
        }

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.enmAdditional = RTFSOBJATTRADD_EASIZE;
            RT_ZERO(pObjInfo->Attr.u);
            break;

        default:
            return VERR_NOT_SUPPORTED;
    }

    return VINF_SUCCESS;
}


/**
 * Xar filesystem base object operations.
 */
static const RTVFSOBJOPS g_rtZipXarFssBaseObjOps =
{
    RTVFSOBJOPS_VERSION,
    RTVFSOBJTYPE_BASE,
    "XarFsStream::Obj",
    rtZipXarFssBaseObj_Close,
    rtZipXarFssBaseObj_QueryInfo,
    NULL,
    RTVFSOBJOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipXarFssIos_Close(void *pvThis)
{
    PRTZIPXARIOSTREAM pThis = (PRTZIPXARIOSTREAM)pvThis;

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    return rtZipXarFssBaseObj_Close(&pThis->BaseObj);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipXarFssIos_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPXARIOSTREAM pThis = (PRTZIPXARIOSTREAM)pvThis;
    return rtZipXarFssBaseObj_QueryInfo(&pThis->BaseObj, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipXarFssIos_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTZIPXARIOSTREAM pThis = (PRTZIPXARIOSTREAM)pvThis;
    AssertReturn(off >= -1, VERR_INVALID_PARAMETER);
    AssertReturn(pSgBuf->cSegs == 1, VERR_INVALID_PARAMETER);

    /*
     * Fend of reads beyond the end of the stream here.  If
     */
    if (off == -1)
        off = pThis->offCurPos;
    if (off < 0 || off > pThis->DataAttr.cbDataArchived)
        return VERR_EOF;
    if (pThis->fEndOfStream)
    {
        if (off >= pThis->DataAttr.cbDataArchived)
            return pcbRead ? VINF_EOF : VERR_EOF;
        if (!pThis->fSeekable)
            return VERR_SEEK_ON_DEVICE;
        pThis->fEndOfStream = false;
    }

    size_t cbToRead = pSgBuf->paSegs[0].cbSeg;
    uint64_t cbLeft = pThis->DataAttr.cbDataArchived - off;
    if (cbToRead > cbLeft)
    {
        if (!pcbRead)
            return VERR_EOF;
        cbToRead = (size_t)cbLeft;
    }

    /*
     * Do the reading.
     */
    size_t cbReadStack = 0;
    if (!pcbRead)
        pcbRead = &cbReadStack;
    int rc = RTVfsIoStrmReadAt(pThis->hVfsIos, off + pThis->DataAttr.offData, pSgBuf->paSegs[0].pvSeg,
                               cbToRead, fBlocking, pcbRead);

    /* Feed the hashes. */
    size_t cbActuallyRead = *pcbRead;
    if (pThis->uHashState == RTZIPXAR_HASH_PENDING)
    {
        if (pThis->offCurPos == pThis->cbDigested)
        {
            rtZipXarHashUpdate(&pThis->CtxArchived,  pThis->DataAttr.uHashFunArchived,  pSgBuf->paSegs[0].pvSeg, cbActuallyRead);
            rtZipXarHashUpdate(&pThis->CtxExtracted, pThis->DataAttr.uHashFunExtracted, pSgBuf->paSegs[0].pvSeg, cbActuallyRead);
            pThis->cbDigested += cbActuallyRead;
        }
        else if (   pThis->cbDigested > pThis->offCurPos
                 && pThis->cbDigested < (RTFOFF)(pThis->offCurPos + cbActuallyRead))
        {
            size_t      offHash = pThis->cbDigested - pThis->offCurPos;
            void const *pvHash  = (uint8_t const *)pSgBuf->paSegs[0].pvSeg + offHash;
            size_t      cbHash  = cbActuallyRead - offHash;
            rtZipXarHashUpdate(&pThis->CtxArchived,  pThis->DataAttr.uHashFunArchived,  pvHash, cbHash);
            rtZipXarHashUpdate(&pThis->CtxExtracted, pThis->DataAttr.uHashFunExtracted, pvHash, cbHash);
            pThis->cbDigested += cbHash;
        }
    }

    /* Update the file position. */
    pThis->offCurPos += cbActuallyRead;
    RTSgBufAdvance(pSgBuf, cbActuallyRead);

    /*
     * Check for end of stream, also check the hash.
     */
    if (pThis->offCurPos >= pThis->DataAttr.cbDataArchived)
    {
        Assert(pThis->offCurPos == pThis->DataAttr.cbDataArchived);
        pThis->fEndOfStream = true;

        /* Check hash. */
        if (   pThis->uHashState == RTZIPXAR_HASH_PENDING
            && pThis->cbDigested == pThis->DataAttr.cbDataArchived)
        {
            RTZIPXARHASHDIGEST Digest;
            rtZipXarHashFinal(&pThis->CtxArchived, pThis->DataAttr.uHashFunArchived, &Digest);
            if (rtZipXarHashIsEqual(pThis->DataAttr.uHashFunArchived, &Digest, &pThis->DataAttr.DigestArchived))
            {
                rtZipXarHashFinal(&pThis->CtxExtracted, pThis->DataAttr.uHashFunExtracted, &Digest);
                if (rtZipXarHashIsEqual(pThis->DataAttr.uHashFunExtracted, &Digest, &pThis->DataAttr.DigestExtracted))
                    pThis->uHashState = RTZIPXAR_HASH_OK;
                else
                {
                    pThis->uHashState = RTZIPXAR_HASH_FAILED_EXTRACTED;
                    rc = VERR_XAR_EXTRACTED_HASH_MISMATCH;
                }
            }
            else
            {
                pThis->uHashState = RTZIPXAR_HASH_FAILED_ARCHIVED;
                rc = VERR_XAR_ARCHIVED_HASH_MISMATCH;
            }
        }
        else if (pThis->uHashState == RTZIPXAR_HASH_FAILED_ARCHIVED)
            rc = VERR_XAR_ARCHIVED_HASH_MISMATCH;
        else if (pThis->uHashState == RTZIPXAR_HASH_FAILED_EXTRACTED)
            rc = VERR_XAR_EXTRACTED_HASH_MISMATCH;
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtZipXarFssIos_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    /* Cannot write to a read-only I/O stream. */
    NOREF(pvThis); NOREF(off); NOREF(pSgBuf); NOREF(fBlocking); NOREF(pcbWritten);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtZipXarFssIos_Flush(void *pvThis)
{
    /* It's a read only stream, nothing dirty to flush. */
    NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtZipXarFssIos_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                                uint32_t *pfRetEvents)
{
    PRTZIPXARIOSTREAM pThis = (PRTZIPXARIOSTREAM)pvThis;

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
static DECLCALLBACK(int) rtZipXarFssIos_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTZIPXARIOSTREAM pThis = (PRTZIPXARIOSTREAM)pvThis;
    *poffActual = pThis->offCurPos;
    return VINF_SUCCESS;
}


/**
 * Xar I/O stream operations.
 */
static const RTVFSIOSTREAMOPS g_rtZipXarFssIosOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "XarFsStream::IoStream",
        rtZipXarFssIos_Close,
        rtZipXarFssIos_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    0,
    rtZipXarFssIos_Read,
    rtZipXarFssIos_Write,
    rtZipXarFssIos_Flush,
    rtZipXarFssIos_PollOne,
    rtZipXarFssIos_Tell,
    NULL /*Skip*/,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipXarFssFile_Close(void *pvThis)
{
    PRTZIPXARFILE pThis = (PRTZIPXARFILE)pvThis;

    RTVfsFileRelease(pThis->hVfsFile);
    pThis->hVfsFile = NIL_RTVFSFILE;

    return rtZipXarFssIos_Close(&pThis->Ios);
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtZipXarFssFile_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    NOREF(pvThis);
    NOREF(fMode);
    NOREF(fMask);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtZipXarFssFile_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                  PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    NOREF(pvThis);
    NOREF(pAccessTime);
    NOREF(pModificationTime);
    NOREF(pChangeTime);
    NOREF(pBirthTime);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtZipXarFssFile_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    NOREF(pvThis);
    NOREF(uid);
    NOREF(gid);
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtZipXarFssFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTZIPXARFILE pThis = (PRTZIPXARFILE)pvThis;

    /* Recalculate the request to RTFILE_SEEK_BEGIN. */
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            break;
        case RTFILE_SEEK_CURRENT:
            offSeek += pThis->Ios.offCurPos;
            break;
        case RTFILE_SEEK_END:
            offSeek = pThis->Ios.DataAttr.cbDataArchived + offSeek;
            break;
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    /* Do limit checks. */
    if (offSeek < 0)
        offSeek = 0;
    else if (offSeek > pThis->Ios.DataAttr.cbDataArchived)
        offSeek = pThis->Ios.DataAttr.cbDataArchived;

    /* Apply and return. */
    pThis->Ios.fEndOfStream = (offSeek >= pThis->Ios.DataAttr.cbDataArchived);
    pThis->Ios.offCurPos    = offSeek;
    if (poffActual)
        *poffActual = offSeek;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtZipXarFssFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTZIPXARFILE pThis = (PRTZIPXARFILE)pvThis;
    *pcbFile = pThis->Ios.DataAttr.cbDataArchived;
    return VINF_SUCCESS;
}


/**
 * Xar file operations.
 */
static const RTVFSFILEOPS g_rtZipXarFssFileOps =
{
    { /* I/O stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "XarFsStream::File",
            rtZipXarFssFile_Close,
            rtZipXarFssIos_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtZipXarFssIos_Read,
        rtZipXarFssIos_Write,
        rtZipXarFssIos_Flush,
        rtZipXarFssIos_PollOne,
        rtZipXarFssIos_Tell,
        NULL /*Skip*/,
        NULL /*ZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtZipXarFssFile_SetMode,
        rtZipXarFssFile_SetTimes,
        rtZipXarFssFile_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtZipXarFssFile_Seek,
    rtZipXarFssFile_QuerySize,
    NULL /*SetSize*/,
    NULL /*QueryMaxSize*/,
    RTVFSFILEOPS_VERSION,
};




/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipXarFssDecompIos_Close(void *pvThis)
{
    PRTZIPXARDECOMPIOS pThis = (PRTZIPXARDECOMPIOS)pvThis;

    RTVfsIoStrmRelease(pThis->hVfsIosDecompressor);
    pThis->hVfsIosDecompressor = NIL_RTVFSIOSTREAM;

    RTVfsIoStrmRelease(pThis->hVfsIosRaw);
    pThis->hVfsIosRaw = NIL_RTVFSIOSTREAM;
    pThis->pIosRaw = NULL;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipXarFssDecompIos_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPXARDECOMPIOS pThis = (PRTZIPXARDECOMPIOS)pvThis;

    int rc = rtZipXarFssBaseObj_QueryInfo(&pThis->pIosRaw->BaseObj, pObjInfo, enmAddAttr);
    pObjInfo->cbObject = pThis->pIosRaw->DataAttr.cbDataExtracted;
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipXarFssDecompIos_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTZIPXARDECOMPIOS pThis = (PRTZIPXARDECOMPIOS)pvThis;
    AssertReturn(pSgBuf->cSegs == 1, VERR_INVALID_PARAMETER);

    /*
     * Enforce the cbDataExtracted limit.
     */
    if (pThis->offCurPos > pThis->pIosRaw->DataAttr.cbDataExtracted)
        return VERR_XAR_EXTRACTED_SIZE_EXCEEDED;

    /*
     * Read the data.
     *
     * ASSUMES the decompressor stream isn't seekable, so we don't have to
     * validate off wrt data digest updating.
     */
    size_t       cbSeg = 0;
    void * const pvSeg = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &cbSeg);
    int rc = RTVfsIoStrmReadAt(pThis->hVfsIosDecompressor, off, pvSeg, cbSeg, fBlocking, pcbRead);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Hash the data.  When reaching the end match against the expected digest.
     */
    size_t const cbActuallyRead = pcbRead ? *pcbRead : cbSeg;
    pThis->offCurPos += cbActuallyRead;
    RTSgBufAdvance(pSgBuf, cbActuallyRead);
    rtZipXarHashUpdate(&pThis->CtxExtracted, pThis->uHashFunExtracted, pSgBuf->paSegs[0].pvSeg, cbActuallyRead);

    if (rc == VINF_EOF)
    {
        if (pThis->offCurPos == pThis->pIosRaw->DataAttr.cbDataExtracted)
        {
            if (pThis->uHashState == RTZIPXAR_HASH_PENDING)
            {
                RTZIPXARHASHDIGEST Digest;
                rtZipXarHashFinal(&pThis->CtxExtracted, pThis->uHashFunExtracted, &Digest);
                if (rtZipXarHashIsEqual(pThis->uHashFunExtracted, &Digest, &pThis->DigestExtracted))
                    pThis->uHashState = RTZIPXAR_HASH_OK;
                else
                {
                    pThis->uHashState = RTZIPXAR_HASH_FAILED_EXTRACTED;
                    rc = VERR_XAR_EXTRACTED_HASH_MISMATCH;
                }
            }
            else if (pThis->uHashState != RTZIPXAR_HASH_OK)
                rc = VERR_XAR_EXTRACTED_HASH_MISMATCH;
        }
        else
            rc = VERR_XAR_EXTRACTED_SIZE_EXCEEDED;

        /* Ensure that the raw stream is also at the end so that both
           message digests are checked. */
        if (RT_SUCCESS(rc))
        {
            if (   pThis->pIosRaw->offCurPos < pThis->pIosRaw->DataAttr.cbDataArchived
                || pThis->pIosRaw->uHashState == RTZIPXAR_HASH_PENDING)
                rc = VERR_XAR_UNUSED_ARCHIVED_DATA;
            else if (pThis->pIosRaw->uHashState != RTZIPXAR_HASH_OK)
                rc = VERR_XAR_ARCHIVED_HASH_MISMATCH;
        }
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtZipXarFssDecompIos_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    /* Cannot write to a read-only I/O stream. */
    NOREF(pvThis); NOREF(off); NOREF(pSgBuf); NOREF(fBlocking); NOREF(pcbWritten);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtZipXarFssDecompIos_Flush(void *pvThis)
{
    /* It's a read only stream, nothing dirty to flush. */
    NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtZipXarFssDecompIos_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                                      uint32_t *pfRetEvents)
{
    PRTZIPXARDECOMPIOS pThis = (PRTZIPXARDECOMPIOS)pvThis;
    return RTVfsIoStrmPoll(pThis->hVfsIosDecompressor, fEvents, cMillies, fIntr, pfRetEvents);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtZipXarFssDecompIos_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTZIPXARDECOMPIOS pThis = (PRTZIPXARDECOMPIOS)pvThis;
    *poffActual = pThis->offCurPos;
    return VINF_SUCCESS;
}


/**
 * Xar I/O stream operations.
 */
static const RTVFSIOSTREAMOPS g_rtZipXarFssDecompIosOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "XarFsStream::DecompIoStream",
        rtZipXarFssDecompIos_Close,
        rtZipXarFssDecompIos_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    0,
    rtZipXarFssDecompIos_Read,
    rtZipXarFssDecompIos_Write,
    rtZipXarFssDecompIos_Flush,
    rtZipXarFssDecompIos_PollOne,
    rtZipXarFssDecompIos_Tell,
    NULL /*Skip*/,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION
};




/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipXarFssSym_Close(void *pvThis)
{
    PRTZIPXARBASEOBJ pThis = (PRTZIPXARBASEOBJ)pvThis;
    return rtZipXarFssBaseObj_Close(pThis);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipXarFssSym_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPXARBASEOBJ pThis = (PRTZIPXARBASEOBJ)pvThis;
    return rtZipXarFssBaseObj_QueryInfo(pThis, pObjInfo, enmAddAttr);
}

/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtZipXarFssSym_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    NOREF(pvThis); NOREF(fMode); NOREF(fMask);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtZipXarFssSym_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                 PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    NOREF(pvThis); NOREF(pAccessTime); NOREF(pModificationTime); NOREF(pChangeTime); NOREF(pBirthTime);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtZipXarFssSym_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    NOREF(pvThis); NOREF(uid); NOREF(gid);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSSYMLINKOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipXarFssSym_Read(void *pvThis, char *pszTarget, size_t cbTarget)
{
    PRTZIPXARBASEOBJ pThis = (PRTZIPXARBASEOBJ)pvThis;
#if 0
    return RTStrCopy(pszTarget, cbXarget, pThis->pXarReader->szTarget);
#else
    RT_NOREF_PV(pThis); RT_NOREF_PV(pszTarget); RT_NOREF_PV(cbTarget);
    return VERR_NOT_IMPLEMENTED;
#endif
}


/**
 * Xar symbolic (and hardlink) operations.
 */
static const RTVFSSYMLINKOPS g_rtZipXarFssSymOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_SYMLINK,
        "XarFsStream::Symlink",
        rtZipXarFssSym_Close,
        rtZipXarFssSym_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSSYMLINKOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSSYMLINKOPS, ObjSet) - RT_UOFFSETOF(RTVFSSYMLINKOPS, Obj),
        rtZipXarFssSym_SetMode,
        rtZipXarFssSym_SetTimes,
        rtZipXarFssSym_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtZipXarFssSym_Read,
    RTVFSSYMLINKOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipXarFss_Close(void *pvThis)
{
    PRTZIPXARFSSTREAM pThis = (PRTZIPXARFSSTREAM)pvThis;

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    RTVfsFileRelease(pThis->hVfsFile);
    pThis->hVfsFile = NIL_RTVFSFILE;

    if (pThis->XarReader.pDoc)
        delete pThis->XarReader.pDoc;
    pThis->XarReader.pDoc = NULL;
    /* The other XarReader fields only point to elements within pDoc. */
    pThis->XarReader.pToc = NULL;
    pThis->XarReader.cCurDepth = 0;
    pThis->XarReader.pCurFile = NULL;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipXarFss_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPXARFSSTREAM pThis = (PRTZIPXARFSSTREAM)pvThis;
    /* Take the lazy approach here, with the sideffect of providing some info
       that is actually kind of useful. */
    return RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnNext}
 */
static DECLCALLBACK(int) rtZipXarFss_Next(void *pvThis, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj)
{
    PRTZIPXARFSSTREAM pThis = (PRTZIPXARFSSTREAM)pvThis;

    /*
     * Check if we've already reached the end in some way.
     */
    if (pThis->fEndOfStream)
        return VERR_EOF;
    if (pThis->rcFatal != VINF_SUCCESS)
        return pThis->rcFatal;

    /*
     * Get the next file element.
     */
    xml::ElementNode const *pCurFile = pThis->XarReader.pCurFile;
    if (pCurFile)
        pThis->XarReader.pCurFile = pCurFile = rtZipXarGetNextFileElement(pCurFile, &pThis->XarReader.cCurDepth);
    else
    {
        pThis->XarReader.cCurDepth  = 0;
        pThis->XarReader.pCurFile   = pCurFile = pThis->XarReader.pToc->findChildElement("file");
    }
    if (!pCurFile)
    {
        pThis->fEndOfStream = true;
        return VERR_EOF;
    }

    /*
     * Retrive the fundamental attributes (elements actually).
     */
    const char *pszName = pCurFile->findChildElementValueP("name");
    const char *pszType = pCurFile->findChildElementValueP("type");
    if (RT_UNLIKELY(!pszName || !pszType))
        return pThis->rcFatal = VERR_XAR_BAD_FILE_ELEMENT;

    /*
     * Validate the filename.  Being a little too paranoid here, perhaps, wrt
     * path separators and escapes...
     */
    if (   !*pszName
        || strchr(pszName, '/')
        || strchr(pszName, '\\')
        || strchr(pszName, ':')
        || !strcmp(pszName, "..") )
        return pThis->rcFatal = VERR_XAR_INVALID_FILE_NAME;

    /*
     * Gather any additional attributes that are essential to the file type,
     * then create the VFS object we're going to return.
     */
    int             rc;
    RTVFSOBJ        hVfsObj;
    RTVFSOBJTYPE    enmType;
    if (!strcmp(pszType, "file"))
    {
        RTZIPXARDATASTREAM DataAttr;
        rc = rtZipXarGetDataStreamAttributes(pCurFile, &DataAttr);
        if (RT_FAILURE(rc))
            return pThis->rcFatal = rc;
        DataAttr.offData += pThis->offZero + pThis->offStart;

        if (   pThis->hVfsFile != NIL_RTVFSFILE
            && DataAttr.enmEncoding == RTZIPXARENCODING_STORE)
        {
            /*
             * The input is seekable and the XAR file isn't compressed, so we
             * can provide a seekable file to the user.
             */
            RTVFSFILE           hVfsFile;
            PRTZIPXARFILE       pFileData;
            rc = RTVfsNewFile(&g_rtZipXarFssFileOps,
                              sizeof(*pFileData),
                              RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                              NIL_RTVFS,
                              NIL_RTVFSLOCK,
                              &hVfsFile,
                              (void **)&pFileData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pFileData->Ios.BaseObj.pFileElem    = pCurFile;
            pFileData->Ios.BaseObj.fModeType    = RTFS_TYPE_FILE;
            pFileData->Ios.DataAttr             = DataAttr;
            pFileData->Ios.offCurPos            = 0;
            pFileData->Ios.fEndOfStream         = false;
            pFileData->Ios.fSeekable            = true;
            pFileData->Ios.uHashState           = RTZIPXAR_HASH_PENDING;
            pFileData->Ios.cbDigested           = 0;
            rtZipXarHashInit(&pFileData->Ios.CtxArchived,  pFileData->Ios.DataAttr.uHashFunArchived);
            rtZipXarHashInit(&pFileData->Ios.CtxExtracted, pFileData->Ios.DataAttr.uHashFunExtracted);

            pFileData->Ios.hVfsIos              = pThis->hVfsIos;
            RTVfsIoStrmRetain(pFileData->Ios.hVfsIos);
            pFileData->hVfsFile                 = pThis->hVfsFile;
            RTVfsFileRetain(pFileData->hVfsFile);

            /* Try avoid double content hashing. */
            if (pFileData->Ios.DataAttr.uHashFunArchived == pFileData->Ios.DataAttr.uHashFunExtracted)
                pFileData->Ios.DataAttr.uHashFunExtracted = XAR_HASH_NONE;

            enmType = RTVFSOBJTYPE_FILE;
            hVfsObj = RTVfsObjFromFile(hVfsFile);
            RTVfsFileRelease(hVfsFile);
        }
        else
        {
            RTVFSIOSTREAM       hVfsIosRaw;
            PRTZIPXARIOSTREAM   pIosData;
            rc = RTVfsNewIoStream(&g_rtZipXarFssIosOps,
                                  sizeof(*pIosData),
                                  RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                  NIL_RTVFS,
                                  NIL_RTVFSLOCK,
                                  &hVfsIosRaw,
                                  (void **)&pIosData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pIosData->BaseObj.pFileElem     = pCurFile;
            pIosData->BaseObj.fModeType     = RTFS_TYPE_FILE;
            pIosData->DataAttr              = DataAttr;
            pIosData->offCurPos             = 0;
            pIosData->fEndOfStream          = false;
            pIosData->fSeekable             = pThis->hVfsFile != NIL_RTVFSFILE;
            pIosData->uHashState            = RTZIPXAR_HASH_PENDING;
            pIosData->cbDigested            = 0;
            rtZipXarHashInit(&pIosData->CtxArchived,  pIosData->DataAttr.uHashFunArchived);
            rtZipXarHashInit(&pIosData->CtxExtracted, pIosData->DataAttr.uHashFunExtracted);

            pIosData->hVfsIos               = pThis->hVfsIos;
            RTVfsIoStrmRetain(pThis->hVfsIos);

            if (   pIosData->DataAttr.enmEncoding != RTZIPXARENCODING_STORE
                && pIosData->DataAttr.enmEncoding != RTZIPXARENCODING_UNSUPPORTED)
            {
                /*
                 * We need to set up a decompression chain.
                 */
                RTVFSIOSTREAM       hVfsIosDecomp;
                PRTZIPXARDECOMPIOS  pIosDecompData;
                rc = RTVfsNewIoStream(&g_rtZipXarFssDecompIosOps,
                                      sizeof(*pIosDecompData),
                                      RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                      NIL_RTVFS,
                                      NIL_RTVFSLOCK,
                                      &hVfsIosDecomp,
                                      (void **)&pIosDecompData);
                if (RT_FAILURE(rc))
                {
                    RTVfsIoStrmRelease(hVfsIosRaw);
                    return pThis->rcFatal = rc;
                }

                pIosDecompData->hVfsIosDecompressor = NIL_RTVFSIOSTREAM;
                pIosDecompData->hVfsIosRaw          = hVfsIosRaw;
                pIosDecompData->pIosRaw             = pIosData;
                pIosDecompData->offCurPos           = 0;
                pIosDecompData->uHashFunExtracted   = DataAttr.uHashFunExtracted;
                pIosDecompData->uHashState = RTZIPXAR_HASH_PENDING;
                rtZipXarHashInit(&pIosDecompData->CtxExtracted, pIosDecompData->uHashFunExtracted);
                pIosDecompData->DigestExtracted     = DataAttr.DigestExtracted;

                /* Tell the raw end to only hash the archived data. */
                pIosData->DataAttr.uHashFunExtracted = XAR_HASH_NONE;

                /*
                 * Hook up the decompressor.
                 */
                switch (DataAttr.enmEncoding)
                {
                    case RTZIPXARENCODING_GZIP:
                        /* Must allow zlib header, all examples I've got seems
                           to be using it rather than the gzip one.  Makes
                           sense as there is no need to repeat the file name
                           and the attributes. */
                        rc = RTZipGzipDecompressIoStream(hVfsIosRaw, RTZIPGZIPDECOMP_F_ALLOW_ZLIB_HDR,
                                                         &pIosDecompData->hVfsIosDecompressor);
                        break;
                    default:
                        rc = VERR_INTERNAL_ERROR_5;
                        break;
                }
                if (RT_FAILURE(rc))
                {
                    RTVfsIoStrmRelease(hVfsIosDecomp);
                    return pThis->rcFatal = rc;
                }

                /* What to return. */
                hVfsObj = RTVfsObjFromIoStream(hVfsIosDecomp);
                RTVfsIoStrmRelease(hVfsIosDecomp);
            }
            else
            {
                /* Try avoid double content hashing. */
                if (pIosData->DataAttr.uHashFunArchived == pIosData->DataAttr.uHashFunExtracted)
                    pIosData->DataAttr.uHashFunExtracted = XAR_HASH_NONE;

                /* What to return. */
                hVfsObj = RTVfsObjFromIoStream(hVfsIosRaw);
                RTVfsIoStrmRelease(hVfsIosRaw);
            }
            enmType = RTVFSOBJTYPE_IO_STREAM;
        }
    }
    else if (!strcmp(pszType, "directory"))
    {
        PRTZIPXARBASEOBJ pBaseObjData;
        rc = RTVfsNewBaseObj(&g_rtZipXarFssBaseObjOps,
                             sizeof(*pBaseObjData),
                             NIL_RTVFS,
                             NIL_RTVFSLOCK,
                             &hVfsObj,
                             (void **)&pBaseObjData);
        if (RT_FAILURE(rc))
            return pThis->rcFatal = rc;

        pBaseObjData->pFileElem  = pCurFile;
        pBaseObjData->fModeType  = RTFS_TYPE_DIRECTORY;

        enmType = RTVFSOBJTYPE_BASE;
    }
    else if (!strcmp(pszType, "symlink"))
    {
        RTVFSSYMLINK        hVfsSym;
        PRTZIPXARBASEOBJ    pBaseObjData;
        rc = RTVfsNewSymlink(&g_rtZipXarFssSymOps,
                             sizeof(*pBaseObjData),
                             NIL_RTVFS,
                             NIL_RTVFSLOCK,
                             &hVfsSym,
                             (void **)&pBaseObjData);
        if (RT_FAILURE(rc))
            return pThis->rcFatal = rc;

        pBaseObjData->pFileElem  = pCurFile;
        pBaseObjData->fModeType  = RTFS_TYPE_SYMLINK;

        enmType = RTVFSOBJTYPE_SYMLINK;
        hVfsObj = RTVfsObjFromSymlink(hVfsSym);
        RTVfsSymlinkRelease(hVfsSym);
    }
    else
        return pThis->rcFatal = VERR_XAR_UNKNOWN_FILE_TYPE;

    /*
     * Set the return data and we're done.
     */
    if (ppszName)
    {
        /* Figure the length. */
        size_t const            cbCurName  = strlen(pszName) + 1;
        size_t                  cbFullName = cbCurName;
        const xml::ElementNode *pAncestor  = pCurFile;
        uint32_t                cLeft      = pThis->XarReader.cCurDepth;
        while (cLeft-- > 0)
        {
            pAncestor = (const xml::ElementNode *)pAncestor->getParent();               Assert(pAncestor);
            const char *pszAncestorName = pAncestor->findChildElementValueP("name");    Assert(pszAncestorName);
            cbFullName += strlen(pszAncestorName) + 1;
        }

        /* Allocate a buffer. */
        char *psz = *ppszName = RTStrAlloc(cbFullName);
        if (!psz)
        {
            RTVfsObjRelease(hVfsObj);
            return VERR_NO_STR_MEMORY;
        }

        /* Construct it, from the end. */
        psz += cbFullName;
        psz -= cbCurName;
        memcpy(psz, pszName, cbCurName);

        pAncestor = pCurFile;
        cLeft = pThis->XarReader.cCurDepth;
        while (cLeft-- > 0)
        {
            pAncestor = (const xml::ElementNode *)pAncestor->getParent();             Assert(pAncestor);
            const char *pszAncestorName = pAncestor->findChildElementValueP("name");  Assert(pszAncestorName);
            *--psz = '/';
            size_t cchAncestorName = strlen(pszAncestorName);
            psz -= cchAncestorName;
            memcpy(psz, pszAncestorName, cchAncestorName);
        }
        Assert(*ppszName == psz);
    }

    if (phVfsObj)
        *phVfsObj = hVfsObj;
    else
        RTVfsObjRelease(hVfsObj);

    if (penmType)
        *penmType = enmType;

    return VINF_SUCCESS;
}



/**
 * Xar filesystem stream operations.
 */
static const RTVFSFSSTREAMOPS rtZipXarFssOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_FS_STREAM,
        "XarFsStream",
        rtZipXarFss_Close,
        rtZipXarFss_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSFSSTREAMOPS_VERSION,
    0,
    rtZipXarFss_Next,
    NULL,
    NULL,
    NULL,
    RTVFSFSSTREAMOPS_VERSION
};



/**
 * TOC validation part 2.
 *
 * Will advance the input stream past the TOC hash and signature data.
 *
 * @returns IPRT status code.
 * @param   pThis       The FS stream instance being created.
 * @param   pXarHdr     The XAR header.
 * @param   pTocDigest  The TOC input data digest.
 */
static int rtZipXarValidateTocPart2(PRTZIPXARFSSTREAM pThis, PCXARHEADER pXarHdr, PCRTZIPXARHASHDIGEST pTocDigest)
{
    int rc;
    RT_NOREF_PV(pXarHdr);

    /*
     * Check that the hash function in the TOC matches the one in the XAR header.
     */
    const xml::ElementNode *pChecksumElem = pThis->XarReader.pToc->findChildElement("checksum");
    if (pChecksumElem)
    {
        const xml::AttributeNode *pAttr = pChecksumElem->findAttribute("style");
        if (!pAttr)
            return VERR_XAR_BAD_CHECKSUM_ELEMENT;

        const char *pszStyle = pAttr->getValue();
        if (!pszStyle)
            return VERR_XAR_BAD_CHECKSUM_ELEMENT;

        uint8_t uHashFunction;
        rc = rtZipXarParseChecksumStyle(pszStyle, &uHashFunction);
        if (RT_FAILURE(rc))
            return rc;
        if (uHashFunction != pThis->uHashFunction)
            return VERR_XAR_HASH_FUNCTION_MISMATCH;

        /*
         * Verify the checksum if we got one.
         */
        if (pThis->uHashFunction != XAR_HASH_NONE)
        {
            RTFOFF   offChecksum;
            RTFOFF   cbChecksum;
            rc = rtZipXarGetOffsetSizeLengthFromElem(pChecksumElem, &offChecksum, &cbChecksum, NULL);
            if (RT_FAILURE(rc))
                return rc;
            if (cbChecksum != (RTFOFF)pThis->cbHashDigest)
                return VERR_XAR_BAD_DIGEST_LENGTH;
            if (offChecksum != 0 && pThis->hVfsFile == NIL_RTVFSFILE)
                return VERR_XAR_NOT_STREAMBLE_ELEMENT_ORDER;

            RTZIPXARHASHDIGEST StoredDigest;
            rc = RTVfsIoStrmReadAt(pThis->hVfsIos, pThis->offZero + offChecksum, &StoredDigest, pThis->cbHashDigest,
                                   true /*fBlocking*/, NULL /*pcbRead*/);
            if (RT_FAILURE(rc))
                return rc;
            if (memcmp(&StoredDigest, pTocDigest, pThis->cbHashDigest))
                return VERR_XAR_TOC_DIGEST_MISMATCH;
        }
    }
    else if (pThis->uHashFunction != XAR_HASH_NONE)
        return VERR_XAR_BAD_CHECKSUM_ELEMENT;

    /*
     * Check the signature, if we got one.
     */
    /** @todo signing. */

    return VINF_SUCCESS;
}


/**
 * Reads and validates the table of content.
 *
 * @returns IPRT status code.
 * @param   hVfsIosIn   The input stream.
 * @param   pXarHdr     The XAR header.
 * @param   pDoc        The TOC XML document.
 * @param   ppTocElem   Where to return the pointer to the TOC element on
 *                      success.
 * @param   pTocDigest  Where to return the TOC digest on success.
 */
static int rtZipXarReadAndValidateToc(RTVFSIOSTREAM hVfsIosIn, PCXARHEADER pXarHdr,
                                      xml::Document *pDoc, xml::ElementNode const **ppTocElem, PRTZIPXARHASHDIGEST pTocDigest)
{
    /*
     * Decompress it, calculating the hash while doing so.
     */
    char *pszOutput = (char *)RTMemTmpAlloc(pXarHdr->cbTocUncompressed + 1);
    if (!pszOutput)
        return VERR_NO_TMP_MEMORY;
    int rc = VERR_NO_TMP_MEMORY;
    void *pvInput = RTMemTmpAlloc(pXarHdr->cbTocCompressed);
    if (pvInput)
    {
        rc = RTVfsIoStrmRead(hVfsIosIn, pvInput, pXarHdr->cbTocCompressed, true /*fBlocking*/,  NULL);
        if (RT_SUCCESS(rc))
        {
            rtZipXarCalcHash(pXarHdr->uHashFunction, pvInput, pXarHdr->cbTocCompressed, pTocDigest);

            size_t cbActual;
            rc = RTZipBlockDecompress(RTZIPTYPE_ZLIB, 0 /*fFlags*/,
                                      pvInput, pXarHdr->cbTocCompressed, NULL,
                                      pszOutput, pXarHdr->cbTocUncompressed, &cbActual);
            if (RT_SUCCESS(rc) && cbActual != pXarHdr->cbTocUncompressed)
                rc = VERR_XAR_TOC_UNCOMP_SIZE_MISMATCH;
        }
        RTMemTmpFree(pvInput);
    }
    if (RT_SUCCESS(rc))
    {
        pszOutput[pXarHdr->cbTocUncompressed] = '\0';

        /*
         * Parse the TOC (XML document) and do some basic validations.
         */
        size_t cchToc = strlen(pszOutput);
        if (   cchToc     == pXarHdr->cbTocUncompressed
            || cchToc + 1 == pXarHdr->cbTocUncompressed)
        {
            rc = RTStrValidateEncoding(pszOutput);
            if (RT_SUCCESS(rc))
            {
                xml::XmlMemParser Parser;
                try
                {
                    Parser.read(pszOutput, cchToc, RTCString("xar-toc.xml"), *pDoc);
                }
                catch (xml::XmlError &)
                {
                    rc = VERR_XAR_TOC_XML_PARSE_ERROR;
                }
                catch (...)
                {
                    rc = VERR_NO_MEMORY;
                }
                if (RT_SUCCESS(rc))
                {
                    xml::ElementNode const *pRootElem = pDoc->getRootElement();
                    xml::ElementNode const *pTocElem  = NULL;
                    if (pRootElem && pRootElem->nameEquals("xar"))
                        pTocElem = pRootElem->findChildElement("toc");
                    if (pTocElem)
                    {
#ifndef USE_STD_LIST_FOR_CHILDREN
                        Assert(pRootElem->getParent() == NULL);
                        Assert(pTocElem->getParent() == pRootElem);
                        if (   !pTocElem->getNextSibiling()
                            && !pTocElem->getPrevSibiling())
#endif
                        {
                            /*
                             * Further parsing and validation is done after the
                             * caller has created an file system stream instance.
                             */
                            *ppTocElem = pTocElem;

                            RTMemTmpFree(pszOutput);
                            return VINF_SUCCESS;
                        }

                        rc = VERR_XML_TOC_ELEMENT_HAS_SIBLINGS;
                    }
                    else
                        rc = VERR_XML_TOC_ELEMENT_MISSING;
                }
            }
            else
                rc = VERR_XAR_TOC_UTF8_ENCODING;
        }
        else
            rc = VERR_XAR_TOC_STRLEN_MISMATCH;
    }

    RTMemTmpFree(pszOutput);
    return rc;
}


/**
 * Reads and validates the XAR header.
 *
 * @returns IPRT status code.
 * @param   hVfsIosIn   The input stream.
 * @param   pXarHdr     Where to return the XAR header in host byte order.
 */
static int rtZipXarReadAndValidateHeader(RTVFSIOSTREAM hVfsIosIn, PXARHEADER pXarHdr)
{
    /*
     * Read it and check the signature.
     */
    int rc = RTVfsIoStrmRead(hVfsIosIn, pXarHdr, sizeof(*pXarHdr), true /*fBlocking*/,  NULL);
    if (RT_FAILURE(rc))
        return rc;
    if (pXarHdr->u32Magic != XAR_HEADER_MAGIC)
        return VERR_XAR_WRONG_MAGIC;

    /*
     * Correct the byte order.
     */
    pXarHdr->cbHeader             = RT_BE2H_U16(pXarHdr->cbHeader);
    pXarHdr->uVersion             = RT_BE2H_U16(pXarHdr->uVersion);
    pXarHdr->cbTocCompressed      = RT_BE2H_U64(pXarHdr->cbTocCompressed);
    pXarHdr->cbTocUncompressed    = RT_BE2H_U64(pXarHdr->cbTocUncompressed);
    pXarHdr->uHashFunction        = RT_BE2H_U32(pXarHdr->uHashFunction);

    /*
     * Validate the header.
     */
    if (pXarHdr->uVersion > XAR_HEADER_VERSION)
        return VERR_XAR_UNSUPPORTED_VERSION;
    if (pXarHdr->cbHeader < sizeof(XARHEADER))
        return VERR_XAR_BAD_HDR_SIZE;
    if (pXarHdr->uHashFunction > XAR_HASH_MAX)
        return VERR_XAR_UNSUPPORTED_HASH_FUNCTION;
    if (pXarHdr->cbTocUncompressed < 16)
        return VERR_XAR_TOC_TOO_SMALL;
    if (pXarHdr->cbTocUncompressed > _4M)
        return VERR_XAR_TOC_TOO_BIG;
    if (pXarHdr->cbTocCompressed > _4M)
        return VERR_XAR_TOC_TOO_BIG_COMPRESSED;

    /*
     * Skip over bytes we don't understand (could be padding).
     */
    if (pXarHdr->cbHeader > sizeof(XARHEADER))
    {
        rc = RTVfsIoStrmSkip(hVfsIosIn, pXarHdr->cbHeader - sizeof(XARHEADER));
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTZipXarFsStreamFromIoStream(RTVFSIOSTREAM hVfsIosIn, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(phVfsFss, VERR_INVALID_HANDLE);
    *phVfsFss = NIL_RTVFSFSSTREAM;
    AssertPtrReturn(hVfsIosIn, VERR_INVALID_HANDLE);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    RTFOFF const offStart = RTVfsIoStrmTell(hVfsIosIn);
    AssertReturn(offStart >= 0, (int)offStart);

    uint32_t cRefs = RTVfsIoStrmRetain(hVfsIosIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Read and validate the header, then uncompress the TOC.
     */
    XARHEADER XarHdr;
    int rc = rtZipXarReadAndValidateHeader(hVfsIosIn, &XarHdr);
    if (RT_SUCCESS(rc))
    {
        xml::Document *pDoc = NULL;
        try         { pDoc = new xml::Document(); }
        catch (...) { }
        if (pDoc)
        {
            RTZIPXARHASHDIGEST      TocDigest;
            xml::ElementNode const *pTocElem = NULL;
            rc = rtZipXarReadAndValidateToc(hVfsIosIn, &XarHdr, pDoc, &pTocElem, &TocDigest);
            if (RT_SUCCESS(rc))
            {
                size_t offZero = RTVfsIoStrmTell(hVfsIosIn);
                if (offZero > 0)
                {
                    /*
                     * Create a file system stream before we continue the parsing.
                     */
                    PRTZIPXARFSSTREAM pThis;
                    RTVFSFSSTREAM     hVfsFss;
                    rc = RTVfsNewFsStream(&rtZipXarFssOps, sizeof(*pThis), NIL_RTVFS, NIL_RTVFSLOCK, RTFILE_O_READ,
                                          &hVfsFss, (void **)&pThis);
                    if (RT_SUCCESS(rc))
                    {
                        pThis->hVfsIos              = hVfsIosIn;
                        pThis->hVfsFile             = RTVfsIoStrmToFile(hVfsIosIn);
                        pThis->offStart             = offStart;
                        pThis->offZero              = offZero;
                        pThis->uHashFunction        = (uint8_t)XarHdr.uHashFunction;
                        switch (pThis->uHashFunction)
                        {
                            case XAR_HASH_MD5:  pThis->cbHashDigest = sizeof(TocDigest.abMd5); break;
                            case XAR_HASH_SHA1: pThis->cbHashDigest = sizeof(TocDigest.abSha1); break;
                            default:            pThis->cbHashDigest = 0; break;
                        }
                        pThis->fEndOfStream         = false;
                        pThis->rcFatal              = VINF_SUCCESS;
                        pThis->XarReader.pDoc       = pDoc;
                        pThis->XarReader.pToc       = pTocElem;
                        pThis->XarReader.pCurFile   = 0;
                        pThis->XarReader.cCurDepth  = 0;

                        /*
                         * Next validation step.
                         */
                        rc = rtZipXarValidateTocPart2(pThis, &XarHdr, &TocDigest);
                        if (RT_SUCCESS(rc))
                        {
                            *phVfsFss = hVfsFss;
                            return VINF_SUCCESS;
                        }

                        RTVfsFsStrmRelease(hVfsFss);
                        return rc;
                    }
                }
                else
                    rc = (int)offZero;
            }
            delete pDoc;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    RTVfsIoStrmRelease(hVfsIosIn);
    return rc;
}

