/* $Id: tarvfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - TAR Virtual Filesystem, Reader.
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
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/poll.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>

#include "tarvfsreader.h"


/**
 * Converts a numeric header field to the C native type.
 *
 * @returns IPRT status code.
 *
 * @param   pszField            The TAR header field.
 * @param   cchField            The length of the field.
 * @param   fOctalOnly          Must be octal.
 * @param   pi64                Where to store the value.
 */
static int rtZipTarHdrFieldToNum(const char *pszField, size_t cchField, bool fOctalOnly, int64_t *pi64)
{
    unsigned char const *puchField   = (unsigned char const *)pszField;
    size_t const         cchFieldOrg = cchField;
    if (   fOctalOnly
        || !(*puchField & 0x80))
    {
        /*
         * Skip leading spaces. Include zeros to save a few slower loops below.
         */
        unsigned char ch;
        while (cchField > 0 && ((ch = *puchField) == ' '|| ch == '0'))
            cchField--, puchField++;

        /*
         * Convert octal digits.
         */
        int64_t i64 = 0;
        while (cchField > 0)
        {
            unsigned char uDigit = *puchField - '0';
            if (uDigit >= 8)
                break;
            i64 <<= 3;
            i64 |= uDigit;

            puchField++;
            cchField--;
        }
        *pi64 = i64;

        /*
         * Was it terminated correctly?
         */
        while (cchField > 0)
        {
            ch = *puchField++;
            if (ch != 0 && ch != ' ')
                return cchField < cchFieldOrg
                     ? VERR_TAR_BAD_NUM_FIELD_TERM
                     : VERR_TAR_BAD_NUM_FIELD;
            cchField--;
        }
    }
    else
    {
        /*
         * The first byte has the bit 7 set to indicate base-256, while bit 6
         * is the signed bit. Bits 5:0 are the most significant value bits.
         */
        uint64_t u64;
        if (!(0x40 & *puchField))
        {
            /* Positive or zero value. */
            u64 = *puchField & 0x3f;
            cchField--;
            puchField++;

            while (cchField-- > 0)
            {
                if (RT_LIKELY(u64 <= (uint64_t)INT64_MAX / 256))
                    u64 = (u64 << 8) | *puchField++;
                else
                    return VERR_TAR_NUM_VALUE_TOO_LARGE;
            }
        }
        else
        {
            /* Negative value (could be used in timestamp). We do manual sign extending here. */
            u64 = (UINT64_MAX << 6) | (*puchField & 0x3f);
            cchField--;
            puchField++;

            while (cchField-- > 0)
            {
                if (RT_LIKELY(u64 >= (uint64_t)(INT64_MIN / 256)))
                    u64 = (u64 << 8) | *puchField++;
                else
                    return VERR_TAR_NUM_VALUE_TOO_LARGE;
            }
        }
        *pi64 = (int64_t)u64;
    }

    return VINF_SUCCESS;
}


/**
 * Validates the TAR header.
 *
 * @returns VINF_SUCCESS if valid, VERR_TAR_ZERO_HEADER if all zeros, and
 *          the appropriate VERR_TAR_XXX otherwise.
 * @param   pTar                The TAR header.
 * @param   penmType            Where to return the type of header on success.
 */
static int rtZipTarHdrValidate(PCRTZIPTARHDR pTar, PRTZIPTARTYPE penmType)
{
    /*
     * Calc the checksum first since this enables us to detect zero headers.
     */
    int32_t i32ChkSum;
    int32_t i32ChkSumSignedAlt;
    if (rtZipTarCalcChkSum(pTar, &i32ChkSum, &i32ChkSumSignedAlt))
        return VERR_TAR_ZERO_HEADER;

    /*
     * Read the checksum field and match the checksums.
     */
    int64_t i64HdrChkSum;
    int rc = rtZipTarHdrFieldToNum(pTar->Common.chksum, sizeof(pTar->Common.chksum), true /*fOctalOnly*/, &i64HdrChkSum);
    if (RT_FAILURE(rc))
        return VERR_TAR_BAD_CHKSUM_FIELD;
    if (   i32ChkSum          != i64HdrChkSum
        && i32ChkSumSignedAlt != i64HdrChkSum) /** @todo test this */
        return VERR_TAR_CHKSUM_MISMATCH;

    /*
     * Detect the TAR type.
     */
    RTZIPTARTYPE enmType;
    if (   pTar->Common.magic[0] == 'u'
        && pTar->Common.magic[1] == 's'
        && pTar->Common.magic[2] == 't'
        && pTar->Common.magic[3] == 'a'
        && pTar->Common.magic[4] == 'r')
    {
/** @todo detect star headers */
        if (   pTar->Common.magic[5]   == '\0'
            && pTar->Common.version[0] == '0'
            && pTar->Common.version[1] == '0')
            enmType = RTZIPTARTYPE_POSIX;
        else if (   pTar->Common.magic[5]   == ' '
                 && pTar->Common.version[0] == ' '
                 && pTar->Common.version[1] == '\0')
            enmType = RTZIPTARTYPE_GNU;
        else if (   pTar->Common.magic[5]   == '\0' /* VMWare ambiguity - they probably mean posix but */
                 && pTar->Common.version[0] == ' '  /*                    got the version wrong. */
                 && pTar->Common.version[1] == '\0')
            enmType = RTZIPTARTYPE_POSIX;
        else
            return VERR_TAR_NOT_USTAR_V00;
    }
    else
        enmType = RTZIPTARTYPE_ANCIENT;
    *penmType = enmType;

    /*
     * Perform some basic checks.
     */
    switch (enmType)
    {
        case RTZIPTARTYPE_POSIX:
            if (   !RT_C_IS_ALNUM(pTar->Common.typeflag)
                && pTar->Common.typeflag != '\0')
                return VERR_TAR_UNKNOWN_TYPE_FLAG;
            break;

        case RTZIPTARTYPE_GNU:
            switch (pTar->Common.typeflag)
            {
                case RTZIPTAR_TF_OLDNORMAL:
                case RTZIPTAR_TF_NORMAL:
                case RTZIPTAR_TF_CONTIG:
                case RTZIPTAR_TF_DIR:
                case RTZIPTAR_TF_CHR:
                case RTZIPTAR_TF_BLK:
                case RTZIPTAR_TF_LINK:
                case RTZIPTAR_TF_SYMLINK:
                case RTZIPTAR_TF_FIFO:
                    break;

                case RTZIPTAR_TF_GNU_LONGLINK:
                case RTZIPTAR_TF_GNU_LONGNAME:
                    break;

                case RTZIPTAR_TF_GNU_DUMPDIR:
                case RTZIPTAR_TF_GNU_MULTIVOL:
                case RTZIPTAR_TF_GNU_SPARSE:
                case RTZIPTAR_TF_GNU_VOLDHR:
                    /** @todo Implement full GNU TAR support. .*/
                    return VERR_TAR_UNSUPPORTED_GNU_HDR_TYPE;

                default:
                    return VERR_TAR_UNKNOWN_TYPE_FLAG;
            }
            break;

        case RTZIPTARTYPE_ANCIENT:
            switch (pTar->Common.typeflag)
            {
                case RTZIPTAR_TF_OLDNORMAL:
                case RTZIPTAR_TF_NORMAL:
                case RTZIPTAR_TF_CONTIG:
                case RTZIPTAR_TF_DIR:
                case RTZIPTAR_TF_LINK:
                case RTZIPTAR_TF_SYMLINK:
                case RTZIPTAR_TF_FIFO:
                    break;
                default:
                    return VERR_TAR_UNKNOWN_TYPE_FLAG;
            }
            break;
        default: /* shut up gcc */
            AssertFailedReturn(VERR_INTERNAL_ERROR_3);
    }

    return VINF_SUCCESS;
}


/**
 * Parses and validates the first TAR header of a archive/file/dir/whatever.
 *
 * @returns IPRT status code.
 * @param   pThis               The TAR reader stat.
 * @param   pTar                The TAR header that has been read.
 * @param   fFirst              Set if this is the first header, otherwise
 *                              clear.
 */
static int rtZipTarReaderParseNextHeader(PRTZIPTARREADER pThis, PCRTZIPTARHDR pHdr, bool fFirst)
{
    int rc;

    /*
     * Basic header validation and detection first.
     */
    RTZIPTARTYPE enmType;
    rc = rtZipTarHdrValidate(pHdr, &enmType);
    if (RT_FAILURE_NP(rc))
    {
        if (rc == VERR_TAR_ZERO_HEADER)
        {
            pThis->cZeroHdrs = 1;
            pThis->enmState = RTZIPTARREADERSTATE_ZERO;
            return VINF_SUCCESS;
        }
        return rc;
    }
    if (fFirst)
    {
        pThis->enmType = enmType;
        if (pThis->enmPrevType == RTZIPTARTYPE_INVALID)
            pThis->enmPrevType = enmType;
    }

    /*
     * Handle the header by type.
     */
    switch (pHdr->Common.typeflag)
    {
        case RTZIPTAR_TF_OLDNORMAL:
        case RTZIPTAR_TF_NORMAL:
        case RTZIPTAR_TF_CONTIG:
        case RTZIPTAR_TF_LINK:
        case RTZIPTAR_TF_SYMLINK:
        case RTZIPTAR_TF_CHR:
        case RTZIPTAR_TF_BLK:
        case RTZIPTAR_TF_FIFO:
        case RTZIPTAR_TF_DIR:
            /*
             * Extract the name first.
             */
            if (!pHdr->Common.name[0])
                return VERR_TAR_EMPTY_NAME;
            if (pThis->enmType == RTZIPTARTYPE_POSIX)
            {
                Assert(pThis->offGnuLongCur == 0); Assert(pThis->szName[0] == '\0');
                pThis->szName[0] = '\0';
                if (pHdr->Posix.prefix[0])
                {
                    rc = RTStrCopyEx(pThis->szName, sizeof(pThis->szName), pHdr->Posix.prefix, sizeof(pHdr->Posix.prefix));
                    AssertRC(rc); /* shall not fail */
                    rc = RTStrCat(pThis->szName, sizeof(pThis->szName), "/");
                    AssertRC(rc); /* ditto */
                }
                rc = RTStrCatEx(pThis->szName, sizeof(pThis->szName), pHdr->Common.name, sizeof(pHdr->Common.name));
                AssertRCReturn(rc, rc);
            }
            else if (pThis->enmType == RTZIPTARTYPE_GNU)
            {
                if (!pThis->szName[0])
                {
                    rc = RTStrCopyEx(pThis->szName, sizeof(pThis->szName), pHdr->Common.name, sizeof(pHdr->Common.name));
                    AssertRCReturn(rc, rc);
                }
            }
            else
            {
                /* Old TAR */
                Assert(pThis->offGnuLongCur == 0); Assert(pThis->szName[0] == '\0');
                rc = RTStrCopyEx(pThis->szName, sizeof(pThis->szName), pHdr->Common.name, sizeof(pHdr->Common.name));
                AssertRCReturn(rc, rc);
            }

            /*
             * Extract the link target.
             */
            if (   pHdr->Common.typeflag == RTZIPTAR_TF_LINK
                || pHdr->Common.typeflag == RTZIPTAR_TF_SYMLINK)
            {
                if (   pThis->enmType == RTZIPTARTYPE_POSIX
                    || pThis->enmType == RTZIPTARTYPE_ANCIENT
                    || (pThis->enmType == RTZIPTARTYPE_GNU && pThis->szTarget[0] == '\0')
                   )
                {
                    Assert(pThis->szTarget[0] == '\0');
                    rc = RTStrCopyEx(pThis->szTarget, sizeof(pThis->szTarget),
                                     pHdr->Common.linkname, sizeof(pHdr->Common.linkname));
                    AssertRCReturn(rc, rc);
                }
            }
            else
                pThis->szTarget[0] = '\0';

            pThis->Hdr = *pHdr;
            break;

        case RTZIPTAR_TF_X_HDR:
        case RTZIPTAR_TF_X_GLOBAL:
            /** @todo implement PAX */
            return VERR_TAR_UNSUPPORTED_PAX_TYPE;

        case RTZIPTAR_TF_SOLARIS_XHDR:
            /** @todo implement solaris / pax attribute lists. */
            return VERR_TAR_UNSUPPORTED_SOLARIS_HDR_TYPE;


        /*
         * A GNU long name or long link is a dummy record followed by one or
         * more 512 byte string blocks holding the long name/link.  The name
         * lenght is encoded in the size field, null terminator included.  If
         * it is a symlink or hard link the long name may be followed by a
         * long link sequence.
         */
        case RTZIPTAR_TF_GNU_LONGNAME:
        case RTZIPTAR_TF_GNU_LONGLINK:
        {
            if (strcmp(pHdr->Gnu.name, "././@LongLink"))
                return VERR_TAR_MALFORMED_GNU_LONGXXXX;

            int64_t cb64;
            rc = rtZipTarHdrFieldToNum(pHdr->Gnu.size, sizeof(pHdr->Gnu.size), false /*fOctalOnly*/, &cb64);
            if (RT_FAILURE(rc) || cb64 < 0 || cb64 > _1M)
                return VERR_TAR_MALFORMED_GNU_LONGXXXX;
            uint32_t cb = (uint32_t)cb64;
            if (cb >= sizeof(pThis->szName))
                return VERR_TAR_NAME_TOO_LONG;

            pThis->cbGnuLongExpect  = cb;
            pThis->offGnuLongCur    = 0;
            pThis->enmState         = pHdr->Common.typeflag == RTZIPTAR_TF_GNU_LONGNAME
                                    ? RTZIPTARREADERSTATE_GNU_LONGNAME
                                    : RTZIPTARREADERSTATE_GNU_LONGLINK;
            break;
        }

        case RTZIPTAR_TF_GNU_DUMPDIR:
        case RTZIPTAR_TF_GNU_MULTIVOL:
        case RTZIPTAR_TF_GNU_SPARSE:
        case RTZIPTAR_TF_GNU_VOLDHR:
            /** @todo Implement or skip GNU headers */
            return VERR_TAR_UNSUPPORTED_GNU_HDR_TYPE;

        default:
            return VERR_TAR_UNKNOWN_TYPE_FLAG;
    }

    return VINF_SUCCESS;
}


/**
 * Parses and validates a TAR header.
 *
 * @returns IPRT status code.
 * @param   pThis               The TAR reader stat.
 * @param   pTar                The TAR header that has been read.
 */
static int rtZipTarReaderParseHeader(PRTZIPTARREADER pThis, PCRTZIPTARHDR pHdr)
{
    switch (pThis->enmState)
    {
        /*
         * The first record for a file/directory/whatever.
         */
        case RTZIPTARREADERSTATE_FIRST:
            pThis->Hdr.Common.typeflag  = 0x7f;
            pThis->enmPrevType          = pThis->enmType;
            pThis->enmType              = RTZIPTARTYPE_INVALID;
            pThis->offGnuLongCur        = 0;
            pThis->cbGnuLongExpect      = 0;
            pThis->szName[0]            = '\0';
            pThis->szTarget[0]          = '\0';
            return rtZipTarReaderParseNextHeader(pThis, pHdr, true /*fFirst*/);

        /*
         * There should only be so many zero headers at the end of the file as
         * it is a function of the block size used when writing.  Don't go on
         * reading them forever in case someone points us to /dev/zero.
         */
        case RTZIPTARREADERSTATE_ZERO:
            if (!ASMMemIsZero(pHdr, sizeof(*pHdr)))
                return VERR_TAR_ZERO_HEADER;
            pThis->cZeroHdrs++;
            if (pThis->cZeroHdrs <= _64K / 512 + 2)
                return VINF_SUCCESS;
            return VERR_TAR_ZERO_HEADER;

        case RTZIPTARREADERSTATE_GNU_LONGNAME:
        case RTZIPTARREADERSTATE_GNU_LONGLINK:
        {
            size_t cbIncoming = RTStrNLen((const char *)pHdr->ab, sizeof(*pHdr));
            if (cbIncoming < sizeof(*pHdr))
                cbIncoming += 1;

            if (cbIncoming + pThis->offGnuLongCur > pThis->cbGnuLongExpect)
                return VERR_TAR_MALFORMED_GNU_LONGXXXX;
            if (   cbIncoming < sizeof(*pHdr)
                && cbIncoming + pThis->offGnuLongCur != pThis->cbGnuLongExpect)
                return VERR_TAR_MALFORMED_GNU_LONGXXXX;

            char *pszDst = pThis->enmState == RTZIPTARREADERSTATE_GNU_LONGNAME ? pThis->szName : pThis->szTarget;
            pszDst += pThis->offGnuLongCur;
            memcpy(pszDst, pHdr->ab, cbIncoming);

            pThis->offGnuLongCur += (uint32_t)cbIncoming;
            if (pThis->offGnuLongCur == pThis->cbGnuLongExpect)
                pThis->enmState = RTZIPTARREADERSTATE_GNU_NEXT;
            return VINF_SUCCESS;
        }

        case RTZIPTARREADERSTATE_GNU_NEXT:
            pThis->enmState = RTZIPTARREADERSTATE_FIRST;
            return rtZipTarReaderParseNextHeader(pThis, pHdr, false /*fFirst*/);

        default:
            return VERR_INTERNAL_ERROR_5;
    }
}


/**
 * Translate a TAR header to an IPRT object info structure with additional UNIX
 * attributes.
 *
 * This completes the validation done by rtZipTarHdrValidate.
 *
 * @returns VINF_SUCCESS if valid, appropriate VERR_TAR_XXX if not.
 * @param   pThis               The TAR reader instance.
 * @param   pObjInfo            The object info structure (output).
 */
static int rtZipTarReaderGetFsObjInfo(PRTZIPTARREADER pThis, PRTFSOBJINFO pObjInfo)
{
    /*
     * Zap the whole structure, this takes care of unused space in the union.
     */
    RT_ZERO(*pObjInfo);

    /*
     * Convert the TAR field in RTFSOBJINFO order.
     */
    int         rc;
    int64_t     i64Tmp;
#define GET_TAR_NUMERIC_FIELD_RET(a_Var, a_Field) \
        do { \
            rc = rtZipTarHdrFieldToNum(a_Field, sizeof(a_Field), false /*fOctalOnly*/, &i64Tmp); \
            if (RT_FAILURE(rc)) \
                return rc; \
            (a_Var) = i64Tmp; \
            if ((a_Var) != i64Tmp) \
                return VERR_TAR_NUM_VALUE_TOO_LARGE; \
        } while (0)

    GET_TAR_NUMERIC_FIELD_RET(pObjInfo->cbObject,        pThis->Hdr.Common.size);
    pObjInfo->cbAllocated = RT_ALIGN_64(pObjInfo->cbObject, 512);
    int64_t c64SecModTime;
    GET_TAR_NUMERIC_FIELD_RET(c64SecModTime,             pThis->Hdr.Common.mtime);
    RTTimeSpecSetSeconds(&pObjInfo->ChangeTime,          c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->ModificationTime,    c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->AccessTime,          c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->BirthTime,           c64SecModTime);
    if (c64SecModTime != RTTimeSpecGetSeconds(&pObjInfo->ModificationTime))
        return VERR_TAR_NUM_VALUE_TOO_LARGE;
    GET_TAR_NUMERIC_FIELD_RET(pObjInfo->Attr.fMode,      pThis->Hdr.Common.mode);
    pObjInfo->Attr.enmAdditional = RTFSOBJATTRADD_UNIX;
    GET_TAR_NUMERIC_FIELD_RET(pObjInfo->Attr.u.Unix.uid, pThis->Hdr.Common.uid);
    GET_TAR_NUMERIC_FIELD_RET(pObjInfo->Attr.u.Unix.gid, pThis->Hdr.Common.gid);
    pObjInfo->Attr.u.Unix.cHardlinks    = 1;
    pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
    pObjInfo->Attr.u.Unix.INodeId       = 0;
    pObjInfo->Attr.u.Unix.fFlags        = 0;
    pObjInfo->Attr.u.Unix.GenerationId  = 0;
    pObjInfo->Attr.u.Unix.Device        = 0;
    switch (pThis->enmType)
    {
        case RTZIPTARTYPE_POSIX:
        case RTZIPTARTYPE_GNU:
            if (   pThis->Hdr.Common.typeflag == RTZIPTAR_TF_CHR
                || pThis->Hdr.Common.typeflag == RTZIPTAR_TF_BLK)
            {
                uint32_t uMajor, uMinor;
                GET_TAR_NUMERIC_FIELD_RET(uMajor,        pThis->Hdr.Common.devmajor);
                GET_TAR_NUMERIC_FIELD_RET(uMinor,        pThis->Hdr.Common.devminor);
                pObjInfo->Attr.u.Unix.Device    = RTDEV_MAKE(uMajor, uMinor);
                if (   uMajor != RTDEV_MAJOR(pObjInfo->Attr.u.Unix.Device)
                    || uMinor != RTDEV_MINOR(pObjInfo->Attr.u.Unix.Device))
                    return VERR_TAR_DEV_VALUE_TOO_LARGE;
            }
            break;

        default:
            if (   pThis->Hdr.Common.typeflag == RTZIPTAR_TF_CHR
                || pThis->Hdr.Common.typeflag == RTZIPTAR_TF_BLK)
                return VERR_TAR_UNKNOWN_TYPE_FLAG;
    }

#undef GET_TAR_NUMERIC_FIELD_RET

    /*
     * Massage the result a little bit.
     * Also validate some more now that we've got the numbers to work with.
     */
    if (   (pObjInfo->Attr.fMode & ~RTFS_UNIX_MASK)
        && pThis->enmType == RTZIPTARTYPE_POSIX)
        return VERR_TAR_BAD_MODE_FIELD;
    pObjInfo->Attr.fMode &= RTFS_UNIX_MASK;

    RTFMODE fModeType = 0;
    switch (pThis->Hdr.Common.typeflag)
    {
        case RTZIPTAR_TF_OLDNORMAL:
        case RTZIPTAR_TF_NORMAL:
        case RTZIPTAR_TF_CONTIG:
        {
            const char *pszEnd = strchr(pThis->szName, '\0');
            if (pszEnd == &pThis->szName[0] || pszEnd[-1] != '/')
                fModeType |= RTFS_TYPE_FILE;
            else
                fModeType |= RTFS_TYPE_DIRECTORY;
            break;
        }

        case RTZIPTAR_TF_LINK:
            if (pObjInfo->cbObject != 0)
#if 0 /* too strict */
                return VERR_TAR_SIZE_NOT_ZERO;
#else
                pObjInfo->cbObject = pObjInfo->cbAllocated = 0;
#endif
            fModeType |= RTFS_TYPE_FILE; /* no better idea for now */
            break;

        case RTZIPTAR_TF_SYMLINK:
            fModeType |= RTFS_TYPE_SYMLINK;
            break;

        case RTZIPTAR_TF_CHR:
            fModeType |= RTFS_TYPE_DEV_CHAR;
            break;

        case RTZIPTAR_TF_BLK:
            fModeType |= RTFS_TYPE_DEV_BLOCK;
            break;

        case RTZIPTAR_TF_DIR:
            fModeType |= RTFS_TYPE_DIRECTORY;
            break;

        case RTZIPTAR_TF_FIFO:
            fModeType |= RTFS_TYPE_FIFO;
            break;

        case RTZIPTAR_TF_GNU_LONGLINK:
        case RTZIPTAR_TF_GNU_LONGNAME:
            /* ASSUMES RTFS_TYPE_XXX uses the same values as GNU stored in the mode field. */
            fModeType = pObjInfo->Attr.fMode & RTFS_TYPE_MASK;
            switch (fModeType)
            {
                case RTFS_TYPE_FILE:
                case RTFS_TYPE_DIRECTORY:
                case RTFS_TYPE_SYMLINK:
                case RTFS_TYPE_DEV_BLOCK:
                case RTFS_TYPE_DEV_CHAR:
                case RTFS_TYPE_FIFO:
                    break;

                default:
                case 0:
                    return VERR_TAR_UNKNOWN_TYPE_FLAG; /** @todo new status code */
            }
            break;

        default:
            return VERR_TAR_UNKNOWN_TYPE_FLAG; /* Should've been caught in validate. */
    }
    if (   (pObjInfo->Attr.fMode & RTFS_TYPE_MASK)
        && (pObjInfo->Attr.fMode & RTFS_TYPE_MASK) != fModeType)
        return VERR_TAR_MODE_WITH_TYPE;
    pObjInfo->Attr.fMode &= ~RTFS_TYPE_MASK;
    pObjInfo->Attr.fMode |= fModeType;

    switch (pThis->Hdr.Common.typeflag)
    {
        case RTZIPTAR_TF_CHR:
        case RTZIPTAR_TF_BLK:
        case RTZIPTAR_TF_DIR:
        case RTZIPTAR_TF_FIFO:
            pObjInfo->cbObject    = 0;
            pObjInfo->cbAllocated = 0;
            break;
    }

    return VINF_SUCCESS;
}


/**
 * Checks if the reader is expecting more headers.
 *
 * @returns true / false.
 * @param   pThis               The TAR reader instance.
 */
static bool rtZipTarReaderExpectingMoreHeaders(PRTZIPTARREADER pThis)
{
    return pThis->enmState != RTZIPTARREADERSTATE_FIRST;
}


/**
 * Checks if we're at the end of the TAR file.
 *
 * @returns true / false.
 * @param   pThis               The TAR reader instance.
 */
static bool rtZipTarReaderIsAtEnd(PRTZIPTARREADER pThis)
{
    /*
     * In theory there shall always be two zero headers at the end of the
     * archive, but life isn't that simple.   We've been creating archives
     * without any zero headers at the end ourselves for a long long time
     * (old tar.cpp).
     *
     * So, we're fine if the state is 'FIRST' or 'ZERO' here, but we'll barf
     * if we're in the middle of a multi-header stream (long GNU names, sparse
     * files, PAX, etc).
     */
    return pThis->enmState == RTZIPTARREADERSTATE_FIRST
        || pThis->enmState == RTZIPTARREADERSTATE_ZERO;
}


/**
 * Checks if the current TAR object is a hard link or not.
 *
 * @returns true if it is, false if not.
 * @param   pThis               The TAR reader instance.
 */
static bool rtZipTarReaderIsHardlink(PRTZIPTARREADER pThis)
{
    return pThis->Hdr.Common.typeflag == RTZIPTAR_TF_LINK;
}


/**
 * Checks if the TAR header includes a POSIX or GNU user name field.
 *
 * @returns true / false.
 * @param   pThis               The TAR reader instance.
 */
DECLINLINE(bool) rtZipTarReaderHasUserName(PRTZIPTARREADER pThis)
{
    return pThis->Hdr.Common.uname[0] != '\0'
        && (   pThis->enmType == RTZIPTARTYPE_POSIX
            || pThis->enmType == RTZIPTARTYPE_GNU);
}


/**
 * Checks if the TAR header includes a POSIX or GNU group name field.
 *
 * @returns true / false.
 * @param   pThis               The TAR reader instance.
 */
DECLINLINE(bool) rtZipTarReaderHasGroupName(PRTZIPTARREADER pThis)
{
    return pThis->Hdr.Common.gname[0] != '\0'
        && (   pThis->enmType == RTZIPTARTYPE_POSIX
            || pThis->enmType == RTZIPTARTYPE_GNU);
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
static DECLCALLBACK(int) rtZipTarFssBaseObj_Close(void *pvThis)
{
    PRTZIPTARBASEOBJ pThis = (PRTZIPTARBASEOBJ)pvThis;

    /* Currently there is nothing we really have to do here. */
    pThis->offHdr = -1;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipTarFssBaseObj_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPTARBASEOBJ pThis = (PRTZIPTARBASEOBJ)pvThis;

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
            pObjInfo->Attr.enmAdditional         = RTFSOBJATTRADD_UNIX_OWNER;
            pObjInfo->Attr.u.UnixOwner.uid       = pThis->ObjInfo.Attr.u.Unix.uid;
            pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            if (rtZipTarReaderHasUserName(pThis->pTarReader))
                RTStrCopy(pObjInfo->Attr.u.UnixOwner.szName, sizeof(pObjInfo->Attr.u.UnixOwner.szName),
                          pThis->pTarReader->Hdr.Common.uname);
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            *pObjInfo = pThis->ObjInfo;
            pObjInfo->Attr.enmAdditional         = RTFSOBJATTRADD_UNIX_GROUP;
            pObjInfo->Attr.u.UnixGroup.gid       = pThis->ObjInfo.Attr.u.Unix.gid;
            pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            if (rtZipTarReaderHasGroupName(pThis->pTarReader))
                RTStrCopy(pObjInfo->Attr.u.UnixGroup.szName, sizeof(pObjInfo->Attr.u.UnixGroup.szName),
                          pThis->pTarReader->Hdr.Common.gname);
            break;

        case RTFSOBJATTRADD_EASIZE:
            *pObjInfo = pThis->ObjInfo;
            pObjInfo->Attr.enmAdditional = RTFSOBJATTRADD_EASIZE;
            RT_ZERO(pObjInfo->Attr.u);
            break;

        default:
            return VERR_NOT_SUPPORTED;
    }

    return VINF_SUCCESS;
}


/**
 * Tar filesystem base object operations.
 */
static const RTVFSOBJOPS g_rtZipTarFssBaseObjOps =
{
    RTVFSOBJOPS_VERSION,
    RTVFSOBJTYPE_BASE,
    "TarFsStream::Obj",
    rtZipTarFssBaseObj_Close,
    rtZipTarFssBaseObj_QueryInfo,
    NULL,
    RTVFSOBJOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipTarFssIos_Close(void *pvThis)
{
    PRTZIPTARIOSTREAM pThis = (PRTZIPTARIOSTREAM)pvThis;

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    return rtZipTarFssBaseObj_Close(&pThis->BaseObj);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipTarFssIos_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPTARIOSTREAM pThis = (PRTZIPTARIOSTREAM)pvThis;
    return rtZipTarFssBaseObj_QueryInfo(&pThis->BaseObj, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipTarFssIos_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTZIPTARIOSTREAM pThis = (PRTZIPTARIOSTREAM)pvThis;
    Assert(pSgBuf->cSegs == 1);

    /*
     * Make offset into a real offset so it's possible to do random access
     * on TAR files that are seekable.  Fend of reads beyond the end of the
     * stream.
     */
    if (off < 0)
        off = pThis->offFile;
    if (off >= pThis->cbFile)
        return pcbRead ? VINF_EOF : VERR_EOF;


    Assert(pThis->cbFile >= pThis->offFile);
    uint64_t cbLeft   = (uint64_t)(pThis->cbFile - off);
    size_t   cbToRead = pSgBuf->paSegs[0].cbSeg;
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
    int rc = RTVfsIoStrmReadAt(pThis->hVfsIos, pThis->offStart + off, pSgBuf->paSegs[0].pvSeg, cbToRead, fBlocking, pcbRead);
    pThis->offFile = off + *pcbRead;
    RTSgBufAdvance(pSgBuf, *pcbRead);

    if (pThis->offFile >= pThis->cbFile)
    {
        Assert(pThis->offFile == pThis->cbFile);
        pThis->fEndOfStream = true;
        RTVfsIoStrmSkip(pThis->hVfsIos, pThis->cbPadding);
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtZipTarFssIos_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    /* Cannot write to a read-only I/O stream. */
    NOREF(pvThis); NOREF(off); NOREF(pSgBuf); NOREF(fBlocking); NOREF(pcbWritten);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtZipTarFssIos_Flush(void *pvThis)
{
    /* It's a read only stream, nothing dirty to flush. */
    NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtZipTarFssIos_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                                uint32_t *pfRetEvents)
{
    PRTZIPTARIOSTREAM pThis = (PRTZIPTARIOSTREAM)pvThis;

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
static DECLCALLBACK(int) rtZipTarFssIos_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTZIPTARIOSTREAM pThis = (PRTZIPTARIOSTREAM)pvThis;
    *poffActual = pThis->offFile;
    return VINF_SUCCESS;
}


/**
 * Tar I/O stream operations.
 */
static const RTVFSIOSTREAMOPS g_rtZipTarFssIosOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "TarFsStream::IoStream",
        rtZipTarFssIos_Close,
        rtZipTarFssIos_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    RTVFSIOSTREAMOPS_FEAT_NO_SG,
    rtZipTarFssIos_Read,
    rtZipTarFssIos_Write,
    rtZipTarFssIos_Flush,
    rtZipTarFssIos_PollOne,
    rtZipTarFssIos_Tell,
    NULL /*Skip*/,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipTarFssSym_Close(void *pvThis)
{
    PRTZIPTARBASEOBJ pThis = (PRTZIPTARBASEOBJ)pvThis;
    return rtZipTarFssBaseObj_Close(pThis);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipTarFssSym_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPTARBASEOBJ pThis = (PRTZIPTARBASEOBJ)pvThis;
    return rtZipTarFssBaseObj_QueryInfo(pThis, pObjInfo, enmAddAttr);
}

/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtZipTarFssSym_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    NOREF(pvThis); NOREF(fMode); NOREF(fMask);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtZipTarFssSym_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                 PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    NOREF(pvThis); NOREF(pAccessTime); NOREF(pModificationTime); NOREF(pChangeTime); NOREF(pBirthTime);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtZipTarFssSym_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    NOREF(pvThis); NOREF(uid); NOREF(gid);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSSYMLINKOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipTarFssSym_Read(void *pvThis, char *pszTarget, size_t cbTarget)
{
    PRTZIPTARBASEOBJ pThis = (PRTZIPTARBASEOBJ)pvThis;
    return RTStrCopy(pszTarget, cbTarget, pThis->pTarReader->szTarget);
}


/**
 * Tar symbolic (and hardlink) operations.
 */
static const RTVFSSYMLINKOPS g_rtZipTarFssSymOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_SYMLINK,
        "TarFsStream::Symlink",
        rtZipTarFssSym_Close,
        rtZipTarFssSym_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSSYMLINKOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSSYMLINKOPS, ObjSet) - RT_UOFFSETOF(RTVFSSYMLINKOPS, Obj),
        rtZipTarFssSym_SetMode,
        rtZipTarFssSym_SetTimes,
        rtZipTarFssSym_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtZipTarFssSym_Read,
    RTVFSSYMLINKOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipTarFss_Close(void *pvThis)
{
    PRTZIPTARFSSTREAM pThis = (PRTZIPTARFSSTREAM)pvThis;

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
static DECLCALLBACK(int) rtZipTarFss_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPTARFSSTREAM pThis = (PRTZIPTARFSSTREAM)pvThis;
    /* Take the lazy approach here, with the sideffect of providing some info
       that is actually kind of useful. */
    return RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnNext}
 */
DECL_HIDDEN_CALLBACK(int) rtZipTarFss_Next(void *pvThis, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj)
{
    PRTZIPTARFSSTREAM pThis = (PRTZIPTARFSSTREAM)pvThis;

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

    /*
     * Make sure the input stream is in the right place.
     */
    RTFOFF offHdr = RTVfsIoStrmTell(pThis->hVfsIos);
    while (   offHdr >= 0
           && offHdr < pThis->offNextHdr)
    {
        int rc = RTVfsIoStrmSkip(pThis->hVfsIos, pThis->offNextHdr - offHdr);
        if (RT_FAILURE(rc))
        {
            /** @todo Ignore if we're at the end of the stream? */
            return pThis->rcFatal = rc;
        }

        offHdr = RTVfsIoStrmTell(pThis->hVfsIos);
    }

    if (offHdr < 0)
        return pThis->rcFatal = (int)offHdr;
    if (offHdr > pThis->offNextHdr)
        return pThis->rcFatal = VERR_INTERNAL_ERROR_3;
    Assert(pThis->offNextHdr == offHdr);
    pThis->offCurHdr = offHdr;

    /*
     * Consume TAR headers.
     */
    size_t cbHdrs = 0;
    int rc;
    do
    {
        /*
         * Read the next header.
         */
        RTZIPTARHDR Hdr;
        size_t cbRead;
        rc = RTVfsIoStrmRead(pThis->hVfsIos, &Hdr, sizeof(Hdr), true /*fBlocking*/, &cbRead);
        if (RT_FAILURE(rc))
            return pThis->rcFatal = rc;
        if (rc == VINF_EOF && cbRead == 0)
        {
            pThis->fEndOfStream = true;
            return rtZipTarReaderIsAtEnd(&pThis->TarReader) ? VERR_EOF : VERR_TAR_UNEXPECTED_EOS;
        }
        if (cbRead != sizeof(Hdr))
            return pThis->rcFatal = VERR_TAR_UNEXPECTED_EOS;

        cbHdrs += sizeof(Hdr);

        /*
         * Parse the it.
         */
        rc = rtZipTarReaderParseHeader(&pThis->TarReader, &Hdr);
        if (RT_FAILURE(rc))
            return pThis->rcFatal = rc;
    } while (rtZipTarReaderExpectingMoreHeaders(&pThis->TarReader));

    pThis->offNextHdr = offHdr + cbHdrs;

    /*
     * Fill an object info structure from the current TAR state.
     */
    RTFSOBJINFO Info;
    rc = rtZipTarReaderGetFsObjInfo(&pThis->TarReader, &Info);
    if (RT_FAILURE(rc))
        return pThis->rcFatal = rc;

    /*
     * Create an object of the appropriate type.
     */
    RTVFSOBJTYPE    enmType;
    RTVFSOBJ        hVfsObj;
    RTFMODE         fType = Info.Attr.fMode & RTFS_TYPE_MASK;
    if (rtZipTarReaderIsHardlink(&pThis->TarReader))
        fType = RTFS_TYPE_SYMLINK;
    switch (fType)
    {
        /*
         * Files are represented by a VFS I/O stream.
         */
        case RTFS_TYPE_FILE:
        {
            RTVFSIOSTREAM       hVfsIos;
            PRTZIPTARIOSTREAM   pIosData;
            rc = RTVfsNewIoStream(&g_rtZipTarFssIosOps,
                                  sizeof(*pIosData),
                                  RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                  NIL_RTVFS,
                                  NIL_RTVFSLOCK,
                                  &hVfsIos,
                                  (void **)&pIosData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pIosData->BaseObj.offHdr     = offHdr;
            pIosData->BaseObj.offNextHdr = pThis->offNextHdr;
            pIosData->BaseObj.pTarReader = &pThis->TarReader;
            pIosData->BaseObj.ObjInfo    = Info;
            pIosData->cbFile             = Info.cbObject;
            pIosData->offFile            = 0;
            pIosData->offStart           = RTVfsIoStrmTell(pThis->hVfsIos);
            pIosData->cbPadding          = (uint32_t)(Info.cbAllocated - Info.cbObject);
            pIosData->fEndOfStream       = false;
            pIosData->hVfsIos            = pThis->hVfsIos;
            RTVfsIoStrmRetain(pThis->hVfsIos);

            pThis->pCurIosData = pIosData;
            pThis->offNextHdr += pIosData->cbFile + pIosData->cbPadding;

            enmType = RTVFSOBJTYPE_IO_STREAM;
            hVfsObj = RTVfsObjFromIoStream(hVfsIos);
            RTVfsIoStrmRelease(hVfsIos);
            break;
        }

        /*
         * We represent hard links using a symbolic link object.  This fits
         * best with the way TAR stores it and there is currently no better
         * fitting VFS type alternative.
         */
        case RTFS_TYPE_SYMLINK:
        {
            RTVFSSYMLINK        hVfsSym;
            PRTZIPTARBASEOBJ    pBaseObjData;
            rc = RTVfsNewSymlink(&g_rtZipTarFssSymOps,
                                 sizeof(*pBaseObjData),
                                 NIL_RTVFS,
                                 NIL_RTVFSLOCK,
                                 &hVfsSym,
                                 (void **)&pBaseObjData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pBaseObjData->offHdr     = offHdr;
            pBaseObjData->offNextHdr = pThis->offNextHdr;
            pBaseObjData->pTarReader = &pThis->TarReader;
            pBaseObjData->ObjInfo    = Info;

            enmType = RTVFSOBJTYPE_SYMLINK;
            hVfsObj = RTVfsObjFromSymlink(hVfsSym);
            RTVfsSymlinkRelease(hVfsSym);
            break;
        }

        /*
         * All other objects are repesented using a VFS base object since they
         * carry no data streams (unless some TAR extension implements extended
         * attributes / alternative streams).
         */
        case RTFS_TYPE_DEV_BLOCK:
        case RTFS_TYPE_DEV_CHAR:
        case RTFS_TYPE_DIRECTORY:
        case RTFS_TYPE_FIFO:
        {
            PRTZIPTARBASEOBJ pBaseObjData;
            rc = RTVfsNewBaseObj(&g_rtZipTarFssBaseObjOps,
                                 sizeof(*pBaseObjData),
                                 NIL_RTVFS,
                                 NIL_RTVFSLOCK,
                                 &hVfsObj,
                                 (void **)&pBaseObjData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pBaseObjData->offHdr     = offHdr;
            pBaseObjData->offNextHdr = pThis->offNextHdr;
            pBaseObjData->pTarReader = &pThis->TarReader;
            pBaseObjData->ObjInfo    = Info;

            enmType = RTVFSOBJTYPE_BASE;
            break;
        }

        default:
            AssertFailed();
            return pThis->rcFatal = VERR_INTERNAL_ERROR_5;
    }
    pThis->hVfsCurObj = hVfsObj;

    /*
     * Set the return data and we're done.
     */
    if (ppszName)
    {
        rc = RTStrDupEx(ppszName, pThis->TarReader.szName);
        if (RT_FAILURE(rc))
            return rc;
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
 * Tar filesystem stream operations.
 */
static const RTVFSFSSTREAMOPS rtZipTarFssOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_FS_STREAM,
        "TarFsStream",
        rtZipTarFss_Close,
        rtZipTarFss_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSFSSTREAMOPS_VERSION,
    0,
    rtZipTarFss_Next,
    NULL,
    NULL,
    NULL,
    RTVFSFSSTREAMOPS_VERSION
};


/**
 * Internal function use both by RTZipTarFsStreamFromIoStream() and by
 * RTZipTarFsStreamForFile() in updating mode.
 */
DECLHIDDEN(void) rtZipTarReaderInit(PRTZIPTARFSSTREAM pThis, RTVFSIOSTREAM hVfsIos, uint64_t offStart)
{
    pThis->hVfsIos              = hVfsIos;
    pThis->hVfsCurObj           = NIL_RTVFSOBJ;
    pThis->pCurIosData          = NULL;
    pThis->offStart             = offStart;
    pThis->offNextHdr           = offStart;
    pThis->fEndOfStream         = false;
    pThis->rcFatal              = VINF_SUCCESS;
    pThis->TarReader.enmPrevType= RTZIPTARTYPE_INVALID;
    pThis->TarReader.enmType    = RTZIPTARTYPE_INVALID;
    pThis->TarReader.enmState   = RTZIPTARREADERSTATE_FIRST;

    /* Don't check if it's a TAR stream here, do that in the
       rtZipTarFss_Next. */
}


RTDECL(int) RTZipTarFsStreamFromIoStream(RTVFSIOSTREAM hVfsIosIn, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss)
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
     * Retain the input stream and create a new filesystem stream handle.
     */
    PRTZIPTARFSSTREAM pThis;
    RTVFSFSSTREAM     hVfsFss;
    int rc = RTVfsNewFsStream(&rtZipTarFssOps, sizeof(*pThis), NIL_RTVFS, NIL_RTVFSLOCK, RTFILE_O_READ,
                              &hVfsFss, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        rtZipTarReaderInit(pThis, hVfsIosIn, fFlags);
        *phVfsFss = hVfsFss;
        return VINF_SUCCESS;
    }

    RTVfsIoStrmRelease(hVfsIosIn);
    return rc;
}


/**
 * Used by RTZipTarFsStreamTruncate to resolve @a hVfsObj.
 */
DECLHIDDEN(PRTZIPTARBASEOBJ) rtZipTarFsStreamBaseObjToPrivate(PRTZIPTARFSSTREAM pThis, RTVFSOBJ hVfsObj)
{
    PRTZIPTARBASEOBJ pThisObj;
    RTVFSOBJTYPE enmType = RTVfsObjGetType(hVfsObj);
    switch (enmType)
    {
        case RTVFSOBJTYPE_IO_STREAM:
        {
            RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
            AssertReturn(hVfsIos != NIL_RTVFSIOSTREAM, NULL);
            PRTZIPTARIOSTREAM pThisStrm = (PRTZIPTARIOSTREAM)RTVfsIoStreamToPrivate(hVfsIos, &g_rtZipTarFssIosOps);
            RTVfsIoStrmRelease(hVfsIos);
            pThisObj = &pThisStrm->BaseObj;
            break;
        }

        case RTVFSOBJTYPE_SYMLINK:
        {
            RTVFSSYMLINK hVfsSymlink = RTVfsObjToSymlink(hVfsObj);
            AssertReturn(hVfsSymlink != NIL_RTVFSSYMLINK, NULL);
            pThisObj = (PRTZIPTARBASEOBJ)RTVfsSymlinkToPrivate(hVfsSymlink, &g_rtZipTarFssSymOps);
            RTVfsSymlinkRelease(hVfsSymlink);
            break;
        }

        case RTVFSOBJTYPE_BASE:
            pThisObj = (PRTZIPTARBASEOBJ)RTVfsObjToPrivate(hVfsObj, &g_rtZipTarFssBaseObjOps);
            break;

        default:
            /** @todo implement.   */
            AssertFailedReturn(NULL);
    }

    AssertReturn(pThisObj->pTarReader == &pThis->TarReader, NULL);
    return pThisObj;
}

