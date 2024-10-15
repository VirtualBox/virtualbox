/* $Id: cpiovfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - CPIO Virtual Filesystem, Reader.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <iprt/poll.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>

#include "cpiovfsreader.h"


/**
 * Converts a octal numeric header field to the C native type.
 *
 * @returns IPRT status code.
 * @param   pachField           The CPIO header field.
 * @param   cchField            The length of the field.
 * @param   pi64                Where to store the value.
 */
static int rtZipCpioHdrOctalFieldToNum(const char *pachField, size_t cchField, int64_t *pi64)
{
    /*
     * Skip leading zeros to save a few slower loops below.
     */
    while (cchField > 0 && *pachField == '0')
        cchField--, pachField++;

    /*
     * Convert octal digits.
     */
    int64_t i64 = 0;
    while (cchField > 0)
    {
        unsigned char uDigit = *pachField - '0';
        if (uDigit >= 8)
            return VERR_TAR_BAD_NUM_FIELD;
        i64 <<= 3;
        i64 |= uDigit;

        pachField++;
        cchField--;
    }
    *pi64 = i64;

    return VINF_SUCCESS;
}


/**
 * Converts a hex character to the appropriate nibble.
 *
 * @returns Nibble of the character.
 * @param   chVal                   The value to convert.
 */
static inline uint8_t rtZipCpioHexToNibble(char chVal)
{
    if (chVal >= '0' && chVal <= '9')
        return chVal - '0';
    else if (chVal >= 'a' && chVal <= 'f')
        return chVal - 'a' + 10;
    else if (chVal >= 'A' && chVal <= 'F')
        return chVal - 'A' + 10;

    return 0xff;
}


/**
 * Converts a hexadecimal numeric header field to the C native type.
 *
 * @returns IPRT status code.
 * @param   pachField           The CPIO header field.
 * @param   cchField            The length of the field.
 * @param   pi64                Where to store the value.
 */
static int rtZipCpioHdrHexFieldToNum(const char *pachField, size_t cchField, int64_t *pi64)
{
    uint64_t u64 = 0;

    while (cchField-- > 0)
    {
        uint8_t bNb = rtZipCpioHexToNibble(*pachField++);

        if (RT_LIKELY(bNb != 0xff))
            u64 = (u64 << 4) | bNb;
        else
            return VERR_TAR_BAD_NUM_FIELD;
    }

    *pi64 = (int64_t)u64;
    return VINF_SUCCESS;
}


/**
 * Parses the given ancient binary header and converts it to an FS object info structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The CPIO reader state.
 * @param   pHdr                The header to convert.
 * @param   pcbFilePath         Where to store the file path size on success.
 * @param   pcbPad              Where to store the number of bytes padded after the header and file path
 *                              before the content begins.
 */
static int rtZipCpioReaderParseHeaderAncientBin(PRTZIPCPIOREADER pThis, PCCPIOHDRBIN pHdr,
                                                uint32_t *pcbFilePath, uint32_t *pcbPad)
{
    RT_NOREF(pThis, pHdr, pcbFilePath, pcbPad);
    return VERR_NOT_SUPPORTED;
}


/**
 * Parses the given SuSv2 ASCII header and converts it to an FS object info structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The CPIO reader state.
 * @param   pHdr                The header to convert.
 * @param   pcbFilePath         Where to store the file path size on success.
 * @param   pcbPad              Where to store the number of bytes padded after the header and file path
 *                              before the content begins.
 */
static int rtZipCpioReaderParseHeaderAsciiSusV2(PRTZIPCPIOREADER pThis, PCCPIOHDRSUSV2 pHdr,
                                                uint32_t *pcbFilePath, uint32_t *pcbPad)
{
    PRTFSOBJINFO pObjInfo = &pThis->ObjInfo;
    int rc;
    int64_t i64Tmp;
    int64_t c64SecModTime;

    pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
    pObjInfo->Attr.u.Unix.Device        = 0;
    pObjInfo->Attr.enmAdditional        = RTFSOBJATTRADD_UNIX;

#define GET_CPIO_NUMERIC_FIELD_RET(a_Var, a_Field) \
        do { \
            rc = rtZipCpioHdrOctalFieldToNum(a_Field, sizeof(a_Field), &i64Tmp); \
            if (RT_FAILURE(rc)) \
                return rc; \
            (a_Var) = i64Tmp; \
            if ((a_Var) != i64Tmp) \
                return VERR_TAR_NUM_VALUE_TOO_LARGE; \
        } while (0)

#define GET_CPIO_NUMERIC_FIELD_RET_U64(a_Var, a_Field) \
        do { \
            rc = rtZipCpioHdrOctalFieldToNum(a_Field, sizeof(a_Field), &i64Tmp); \
            if (RT_FAILURE(rc)) \
                return rc; \
            (a_Var) = (uint64_t)i64Tmp; \
            if ((a_Var) != (uint64_t)i64Tmp) \
                return VERR_TAR_NUM_VALUE_TOO_LARGE; \
        } while (0)

    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.fMode,             pHdr->achMode);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.u.Unix.uid,        pHdr->achUid);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.u.Unix.gid,        pHdr->achGid);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.u.Unix.cHardlinks, pHdr->achNLinks);
    GET_CPIO_NUMERIC_FIELD_RET_U64(pObjInfo->Attr.u.Unix.INodeId,    pHdr->achInode);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.u.Unix.Device,     pHdr->achDev);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->cbObject,               pHdr->achFileSize);
    pObjInfo->cbAllocated = pObjInfo->cbObject;
    GET_CPIO_NUMERIC_FIELD_RET(    c64SecModTime,                    pHdr->achMTime);
    RTTimeSpecSetSeconds(&pObjInfo->ChangeTime,                  c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->ModificationTime,            c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->AccessTime,                  c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->BirthTime,                   c64SecModTime);
    if (c64SecModTime != RTTimeSpecGetSeconds(&pObjInfo->ModificationTime))
        return VERR_TAR_NUM_VALUE_TOO_LARGE;

    GET_CPIO_NUMERIC_FIELD_RET(*pcbFilePath, pHdr->achNameSize);

    /* There is never any padding. */
    *pcbPad = 0;

#undef GET_CPIO_NUMERIC_FIELD_RET
#undef GET_CPIO_NUMERIC_FIELD_RET_U64

    return rc;
}


/**
 * Parses the given "new" ASCII header and converts it to an FS object info structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The CPIO reader state.
 * @param   pHdr                The header to convert.
 * @param   fWithChksum         Flag whether the header uses the checksum field.
 * @param   pcbFilePath         Where to store the file path size on success.
 * @param   pcbPad              Where to store the number of bytes padded after the header and file path
 *                              before the content begins.
 */
static int rtZipCpioReaderParseHeaderAsciiNew(PRTZIPCPIOREADER pThis, PCCPIOHDRNEW pHdr, bool fWithChksum,
                                              uint32_t *pcbFilePath, uint32_t *pcbPad)
{
    RT_NOREF(fWithChksum); /** @todo */
    PRTFSOBJINFO pObjInfo = &pThis->ObjInfo;
    int rc;
    int64_t i64Tmp;
    int64_t c64SecModTime;
    uint32_t uMajor, uMinor;

    pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
    pObjInfo->Attr.u.Unix.Device        = 0;
    pObjInfo->Attr.enmAdditional        = RTFSOBJATTRADD_UNIX;

#define GET_CPIO_NUMERIC_FIELD_RET(a_Var, a_Field) \
        do { \
            rc = rtZipCpioHdrHexFieldToNum(a_Field, sizeof(a_Field), &i64Tmp); \
            if (RT_FAILURE(rc)) \
                return rc; \
            (a_Var) = i64Tmp; \
            if ((a_Var) != i64Tmp) \
                return VERR_TAR_NUM_VALUE_TOO_LARGE; \
        } while (0)

#define GET_CPIO_NUMERIC_FIELD_RET_U64(a_Var, a_Field) \
        do { \
            rc = rtZipCpioHdrHexFieldToNum(a_Field, sizeof(a_Field), &i64Tmp); \
            if (RT_FAILURE(rc)) \
                return rc; \
            (a_Var) = (uint64_t)i64Tmp; \
            if ((a_Var) != (uint64_t)i64Tmp) \
                return VERR_TAR_NUM_VALUE_TOO_LARGE; \
        } while (0)

    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.fMode,             pHdr->achMode);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.u.Unix.uid,        pHdr->achUid);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.u.Unix.gid,        pHdr->achGid);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->Attr.u.Unix.cHardlinks, pHdr->achNLinks);
    GET_CPIO_NUMERIC_FIELD_RET_U64(pObjInfo->Attr.u.Unix.INodeId,    pHdr->achInode);
    GET_CPIO_NUMERIC_FIELD_RET(    uMajor,                           pHdr->achDevMajor);
    GET_CPIO_NUMERIC_FIELD_RET(    uMinor,                           pHdr->achDevMinor);
    GET_CPIO_NUMERIC_FIELD_RET(    pObjInfo->cbObject,               pHdr->achFileSize);
    pObjInfo->cbAllocated = RT_ALIGN_64(pObjInfo->cbObject, 4);
    GET_CPIO_NUMERIC_FIELD_RET(    c64SecModTime,                    pHdr->achMTime);
    RTTimeSpecSetSeconds(&pObjInfo->ChangeTime,                  c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->ModificationTime,            c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->AccessTime,                  c64SecModTime);
    RTTimeSpecSetSeconds(&pObjInfo->BirthTime,                   c64SecModTime);
    if (c64SecModTime != RTTimeSpecGetSeconds(&pObjInfo->ModificationTime))
        return VERR_TAR_NUM_VALUE_TOO_LARGE;
    pObjInfo->Attr.u.Unix.Device = RTDEV_MAKE(uMajor, uMinor);
    if (   uMajor != RTDEV_MAJOR(pObjInfo->Attr.u.Unix.Device)
        || uMinor != RTDEV_MINOR(pObjInfo->Attr.u.Unix.Device))
        return VERR_TAR_DEV_VALUE_TOO_LARGE;

    GET_CPIO_NUMERIC_FIELD_RET(*pcbFilePath, pHdr->achNameSize);

    /* Header and file path are padded with 0 bytes to a 4 byte boundary. */
    uint32_t cbComp = *pcbFilePath + sizeof(*pHdr);
    *pcbPad = RT_ALIGN_32(cbComp, 4) - cbComp;

#undef GET_CPIO_NUMERIC_FIELD_RET
#undef GET_CPIO_NUMERIC_FIELD_RET_U64

    return rc;
}


/**
 * Parses and validates a CPIO header.
 *
 * @returns IPRT status code.
 * @param   pThis               The CPIO reader state.
 * @param   enmType             The CPIO header type.
 * @param   pHdr                The CPIO header that has been read.
 * @param   pcbFilePath         Where to store the size of the file path on success.
 * @param   pcbPad              Where to store the number of bytes padded after the header and file path
 *                              before the content begins.
 */
static int rtZipCpioReaderParseHeader(PRTZIPCPIOREADER pThis, RTZIPCPIOTYPE enmType, PCCPIOHDR pHdr,
                                      uint32_t *pcbFilePath, uint32_t *pcbPad)
{
    int rc;

    switch (enmType)
    {
        case RTZIPCPIOTYPE_ANCIENT_BIN:
            rc = rtZipCpioReaderParseHeaderAncientBin(pThis, &pHdr->AncientBin,
                                                      pcbFilePath, pcbPad);
            break;
        case RTZIPCPIOTYPE_ASCII_SUSV2:
            rc = rtZipCpioReaderParseHeaderAsciiSusV2(pThis, &pHdr->AsciiSuSv2,
                                                      pcbFilePath, pcbPad);
            break;
        case RTZIPCPIOTYPE_ASCII_NEW:
            rc = rtZipCpioReaderParseHeaderAsciiNew(pThis, &pHdr->AsciiNew, false /*fWithChksum*/,
                                                    pcbFilePath, pcbPad);
            break;
        case RTZIPCPIOTYPE_ASCII_NEW_CHKSUM:
            rc = rtZipCpioReaderParseHeaderAsciiNew(pThis, &pHdr->AsciiNew, true /*fWithChksum*/,
                                                    pcbFilePath, pcbPad);
            break;
        default:
            AssertMsgFailedBreakStmt(("Invalid CPIO type %d\n", enmType), rc = VERR_INTERNAL_ERROR);
    }

    return rc;
}


/**
 * Reads the file path from the CPIO archive stream.
 *
 * @returns IPRT status code.
 * @param   hVfsIos             The I/O stream to read from.
 * @param   pThis               The CPIO reader state.
 * @param   cbFilePath          Size of the file path in bytes.
 */
static int rtZipCpioReaderReadPath(RTVFSIOSTREAM hVfsIos, PRTZIPCPIOREADER pThis, size_t cbFilePath)
{
    if (cbFilePath >= sizeof(pThis->szName))
        return VERR_TAR_NAME_TOO_LONG;

    size_t cbRead;
    int rc = RTVfsIoStrmRead(hVfsIos, &pThis->szName[0], cbFilePath, true /*fBlocking*/, &cbRead);
    if (RT_FAILURE(rc))
        return rc;
    if (cbRead != cbFilePath)
        return VERR_TAR_UNEXPECTED_EOS;

    /* The read file name should be zero terminated at the end. */
    if (pThis->szName[cbFilePath - 1] != '\0')
        return VERR_TAR_MALFORMED_GNU_LONGXXXX;

    return VINF_SUCCESS;
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
static DECLCALLBACK(int) rtZipCpioFssBaseObj_Close(void *pvThis)
{
    PRTZIPCPIOBASEOBJ pThis = (PRTZIPCPIOBASEOBJ)pvThis;

    /* Currently there is nothing we really have to do here. */
    pThis->offHdr = -1;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipCpioFssBaseObj_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPCPIOBASEOBJ pThis = (PRTZIPCPIOBASEOBJ)pvThis;

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
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            *pObjInfo = pThis->ObjInfo;
            pObjInfo->Attr.enmAdditional         = RTFSOBJATTRADD_UNIX_GROUP;
            pObjInfo->Attr.u.UnixGroup.gid       = pThis->ObjInfo.Attr.u.Unix.gid;
            pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
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
static const RTVFSOBJOPS g_rtZipCpioFssBaseObjOps =
{
    RTVFSOBJOPS_VERSION,
    RTVFSOBJTYPE_BASE,
    "CpioFsStream::Obj",
    rtZipCpioFssBaseObj_Close,
    rtZipCpioFssBaseObj_QueryInfo,
    NULL,
    RTVFSOBJOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipCpioFssIos_Close(void *pvThis)
{
    PRTZIPCPIOIOSTREAM pThis = (PRTZIPCPIOIOSTREAM)pvThis;

    RTVfsIoStrmRelease(pThis->hVfsIos);
    pThis->hVfsIos = NIL_RTVFSIOSTREAM;

    return rtZipCpioFssBaseObj_Close(&pThis->BaseObj);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipCpioFssIos_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPCPIOIOSTREAM pThis = (PRTZIPCPIOIOSTREAM)pvThis;
    return rtZipCpioFssBaseObj_QueryInfo(&pThis->BaseObj, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipCpioFssIos_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTZIPCPIOIOSTREAM pThis = (PRTZIPCPIOIOSTREAM)pvThis;
    Assert(pSgBuf->cSegs == 1);

    /*
     * Make offset into a real offset so it's possible to do random access
     * on CPIO files that are seekable.  Fend of reads beyond the end of the
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
static DECLCALLBACK(int) rtZipCpioFssIos_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    /* Cannot write to a read-only I/O stream. */
    NOREF(pvThis); NOREF(off); NOREF(pSgBuf); NOREF(fBlocking); NOREF(pcbWritten);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtZipCpioFssIos_Flush(void *pvThis)
{
    /* It's a read only stream, nothing dirty to flush. */
    NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnPollOne}
 */
static DECLCALLBACK(int) rtZipCpioFssIos_PollOne(void *pvThis, uint32_t fEvents, RTMSINTERVAL cMillies, bool fIntr,
                                                 uint32_t *pfRetEvents)
{
    PRTZIPCPIOIOSTREAM pThis = (PRTZIPCPIOIOSTREAM)pvThis;

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
static DECLCALLBACK(int) rtZipCpioFssIos_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTZIPCPIOIOSTREAM pThis = (PRTZIPCPIOIOSTREAM)pvThis;
    *poffActual = pThis->offFile;
    return VINF_SUCCESS;
}


/**
 * Tar I/O stream operations.
 */
static const RTVFSIOSTREAMOPS g_rtZipCpioFssIosOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_IO_STREAM,
        "CpioFsStream::IoStream",
        rtZipCpioFssIos_Close,
        rtZipCpioFssIos_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSIOSTREAMOPS_VERSION,
    RTVFSIOSTREAMOPS_FEAT_NO_SG,
    rtZipCpioFssIos_Read,
    rtZipCpioFssIos_Write,
    rtZipCpioFssIos_Flush,
    rtZipCpioFssIos_PollOne,
    rtZipCpioFssIos_Tell,
    NULL /*Skip*/,
    NULL /*ZeroFill*/,
    RTVFSIOSTREAMOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipCpioFssSym_Close(void *pvThis)
{
    PRTZIPCPIOBASEOBJ pThis = (PRTZIPCPIOBASEOBJ)pvThis;
    return rtZipCpioFssBaseObj_Close(pThis);
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtZipCpioFssSym_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPCPIOBASEOBJ pThis = (PRTZIPCPIOBASEOBJ)pvThis;
    return rtZipCpioFssBaseObj_QueryInfo(pThis, pObjInfo, enmAddAttr);
}

/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtZipCpioFssSym_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    NOREF(pvThis); NOREF(fMode); NOREF(fMask);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtZipCpioFssSym_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                  PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    NOREF(pvThis); NOREF(pAccessTime); NOREF(pModificationTime); NOREF(pChangeTime); NOREF(pBirthTime);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtZipCpioFssSym_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    NOREF(pvThis); NOREF(uid); NOREF(gid);
    return VERR_ACCESS_DENIED;
}


/**
 * @interface_method_impl{RTVFSSYMLINKOPS,pfnRead}
 */
static DECLCALLBACK(int) rtZipCpioFssSym_Read(void *pvThis, char *pszTarget, size_t cbTarget)
{
    PRTZIPCPIOBASEOBJ pThis = (PRTZIPCPIOBASEOBJ)pvThis;
    return RTStrCopy(pszTarget, cbTarget, pThis->pCpioReader->szTarget);
}


/**
 * CPIO symbolic (and hardlink) operations.
 */
static const RTVFSSYMLINKOPS g_rtZipCpioFssSymOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_SYMLINK,
        "CpioFsStream::Symlink",
        rtZipCpioFssSym_Close,
        rtZipCpioFssSym_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSSYMLINKOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSSYMLINKOPS, ObjSet) - RT_UOFFSETOF(RTVFSSYMLINKOPS, Obj),
        rtZipCpioFssSym_SetMode,
        rtZipCpioFssSym_SetTimes,
        rtZipCpioFssSym_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtZipCpioFssSym_Read,
    RTVFSSYMLINKOPS_VERSION
};


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtZipCpioFss_Close(void *pvThis)
{
    PRTZIPCPIOFSSTREAM pThis = (PRTZIPCPIOFSSTREAM)pvThis;

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
static DECLCALLBACK(int) rtZipCpioFss_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTZIPCPIOFSSTREAM pThis = (PRTZIPCPIOFSSTREAM)pvThis;
    /* Take the lazy approach here, with the sideffect of providing some info
       that is actually kind of useful. */
    return RTVfsIoStrmQueryInfo(pThis->hVfsIos, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnNext}
 */
DECL_HIDDEN_CALLBACK(int) rtZipCpioFss_Next(void *pvThis, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj)
{
    PRTZIPCPIOFSSTREAM pThis = (PRTZIPCPIOFSSTREAM)pvThis;

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
     * Consume CPIO headers.
     */
    size_t cbHdr = 0;
    /*
     * Read the next header.
     *
     * Read the first 6 bytes to determine the header type and continue reading the
     * rest of the header.
     */
    CPIOHDR Hdr;
    RTZIPCPIOTYPE enmHdrType = RTZIPCPIOTYPE_INVALID;
    size_t cbRead;
    int rc = RTVfsIoStrmRead(pThis->hVfsIos, &Hdr.ab[0], sizeof(Hdr.AsciiNew.achMagic), true /*fBlocking*/, &cbRead);
    if (RT_FAILURE(rc))
        return pThis->rcFatal = rc;
    if (rc == VINF_EOF && cbRead == 0)
    {
        pThis->fEndOfStream = true;
        return VERR_EOF;
    }
    if (cbRead != sizeof(Hdr.AsciiNew.achMagic))
        return pThis->rcFatal = VERR_TAR_UNEXPECTED_EOS;

    if (Hdr.AncientBin.u16Magic == CPIO_HDR_BIN_MAGIC)
    {
        cbHdr = sizeof(Hdr.AncientBin);
        enmHdrType = RTZIPCPIOTYPE_ANCIENT_BIN;
    }
    else if (!strncmp(&Hdr.AsciiSuSv2.achMagic[0], CPIO_HDR_SUSV2_MAGIC, sizeof(Hdr.AsciiSuSv2.achMagic)))
    {
        cbHdr = sizeof(Hdr.AsciiSuSv2);
        enmHdrType = RTZIPCPIOTYPE_ASCII_SUSV2;
    }
    else if (!strncmp(&Hdr.AsciiNew.achMagic[0], CPIO_HDR_NEW_MAGIC, sizeof(Hdr.AsciiNew.achMagic)))
    {
        cbHdr = sizeof(Hdr.AsciiNew);
        enmHdrType = RTZIPCPIOTYPE_ASCII_NEW;
    }
    else if (!strncmp(&Hdr.AsciiNew.achMagic[0], CPIO_HDR_NEW_CHKSUM_MAGIC, sizeof(Hdr.AsciiNew.achMagic)))
    {
        cbHdr = sizeof(Hdr.AsciiNew);
        enmHdrType = RTZIPCPIOTYPE_ASCII_NEW_CHKSUM;
    }
    else
        return pThis->rcFatal = VERR_TAR_UNKNOWN_TYPE_FLAG; /** @todo Dedicated status code. */

    /* Read the rest of the header. */
    size_t cbHdrLeft = cbHdr - sizeof(Hdr.AsciiNew.achMagic);
    rc = RTVfsIoStrmRead(pThis->hVfsIos, &Hdr.ab[sizeof(Hdr.AsciiNew.achMagic)], cbHdr - sizeof(Hdr.AsciiNew.achMagic), true /*fBlocking*/, &cbRead);
    if (RT_FAILURE(rc))
        return pThis->rcFatal = rc;
    if (cbRead != cbHdrLeft)
        return pThis->rcFatal = VERR_TAR_UNEXPECTED_EOS;

    /*
     * Parse it.
     */
    uint32_t cbFilePath = 0;
    uint32_t cbPad = 0;
    rc = rtZipCpioReaderParseHeader(&pThis->CpioReader, enmHdrType, &Hdr, &cbFilePath, &cbPad);
    if (RT_FAILURE(rc))
        return pThis->rcFatal = rc;

    /* Read the file path following the header. */
    rc = rtZipCpioReaderReadPath(pThis->hVfsIos, &pThis->CpioReader, cbFilePath);
    if (RT_FAILURE(rc))
        return pThis->rcFatal = rc;

    if (cbPad)
        RTVfsIoStrmSkip(pThis->hVfsIos, cbPad);
    pThis->offNextHdr = offHdr + cbHdr + cbFilePath + cbPad;

    /*
     * CPIO uses a special trailer file record with a 0 mode and size and using a special
     * marker filename. The filesystem stream is marked EOS When such a record is encountered
     * to not try to read anything which might come behind it, imagine an initramfs image consisting
     * of multiple archives which don't need to be necessarily be all of the CPIO kind (yes, this is
     * a reality with ubuntu for example containing microcode updates as seperate CPIO archives
     * coming before the main LZ4 compressed CPIO archive...).
     */
    PCRTFSOBJINFO pInfo = &pThis->CpioReader.ObjInfo;
    if (RT_UNLIKELY(   !pInfo->Attr.fMode
                    && !pInfo->cbAllocated
                    && !strcmp(&pThis->CpioReader.szName[0], CPIO_EOS_FILE_NAME)))
    {
        pThis->fEndOfStream = true;
        return VERR_EOF;
    }

    /*
     * Create an object of the appropriate type.
     */
    RTVFSOBJTYPE    enmType;
    RTVFSOBJ        hVfsObj;
    RTFMODE         fType = pInfo->Attr.fMode & RTFS_TYPE_MASK;
    switch (fType)
    {
        /*
         * Files are represented by a VFS I/O stream, hardlinks have their content
         * embedded as it is another file.
         */
        case RTFS_TYPE_FILE:
        {
            RTVFSIOSTREAM       hVfsIos;
            PRTZIPCPIOIOSTREAM  pIosData;
            rc = RTVfsNewIoStream(&g_rtZipCpioFssIosOps,
                                  sizeof(*pIosData),
                                  RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                  NIL_RTVFS,
                                  NIL_RTVFSLOCK,
                                  &hVfsIos,
                                  (void **)&pIosData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pIosData->BaseObj.offHdr      = offHdr;
            pIosData->BaseObj.offNextHdr  = pThis->offNextHdr;
            pIosData->BaseObj.pCpioReader = &pThis->CpioReader;
            pIosData->BaseObj.ObjInfo     = *pInfo;
            pIosData->cbFile              = pInfo->cbObject;
            pIosData->offFile             = 0;
            pIosData->offStart            = RTVfsIoStrmTell(pThis->hVfsIos);
            pIosData->cbPadding           = (uint32_t)(pInfo->cbAllocated - pInfo->cbObject);
            pIosData->fEndOfStream        = false;
            pIosData->hVfsIos             = pThis->hVfsIos;
            RTVfsIoStrmRetain(pThis->hVfsIos);

            pThis->pCurIosData = pIosData;
            pThis->offNextHdr += pIosData->cbFile + pIosData->cbPadding;

            enmType = RTVFSOBJTYPE_IO_STREAM;
            hVfsObj = RTVfsObjFromIoStream(hVfsIos);
            RTVfsIoStrmRelease(hVfsIos);
            break;
        }

        case RTFS_TYPE_SYMLINK:
        {
            RTVFSSYMLINK        hVfsSym;
            PRTZIPCPIOBASEOBJ   pBaseObjData;
            rc = RTVfsNewSymlink(&g_rtZipCpioFssSymOps,
                                 sizeof(*pBaseObjData),
                                 NIL_RTVFS,
                                 NIL_RTVFSLOCK,
                                 &hVfsSym,
                                 (void **)&pBaseObjData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pBaseObjData->offHdr      = offHdr;
            pBaseObjData->offNextHdr  = pThis->offNextHdr;
            pBaseObjData->pCpioReader = &pThis->CpioReader;
            pBaseObjData->ObjInfo     = *pInfo;

            /* Read the body of the symlink (as normal file data). */
            if (pInfo->cbObject + 1 > (RTFOFF)sizeof(pThis->CpioReader.szTarget))
                return VERR_TAR_NAME_TOO_LONG;

            cbPad = (uint32_t)(pInfo->cbAllocated - pInfo->cbObject);
            rc = RTVfsIoStrmRead(pThis->hVfsIos, &pThis->CpioReader.szTarget[0], pInfo->cbObject, true /*fBlocking*/, &cbRead);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;
            if (cbRead != (uint32_t)pInfo->cbObject)
                return pThis->rcFatal = VERR_TAR_UNEXPECTED_EOS;

            pThis->CpioReader.szTarget[pInfo->cbObject] = '\0';

            if (cbPad)
               rc = RTVfsIoStrmSkip(pThis->hVfsIos, cbPad);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pThis->offNextHdr += pInfo->cbAllocated;

            enmType = RTVFSOBJTYPE_SYMLINK;
            hVfsObj = RTVfsObjFromSymlink(hVfsSym);
            RTVfsSymlinkRelease(hVfsSym);
            break;
        }

        /*
         * All other objects are repesented using a VFS base object since they
         * carry no data streams.
         */
        case RTFS_TYPE_DEV_BLOCK:
        case RTFS_TYPE_DEV_CHAR:
        case RTFS_TYPE_DIRECTORY:
        case RTFS_TYPE_FIFO:
        {
            PRTZIPCPIOBASEOBJ pBaseObjData;
            rc = RTVfsNewBaseObj(&g_rtZipCpioFssBaseObjOps,
                                 sizeof(*pBaseObjData),
                                 NIL_RTVFS,
                                 NIL_RTVFSLOCK,
                                 &hVfsObj,
                                 (void **)&pBaseObjData);
            if (RT_FAILURE(rc))
                return pThis->rcFatal = rc;

            pBaseObjData->offHdr      = offHdr;
            pBaseObjData->offNextHdr  = pThis->offNextHdr;
            pBaseObjData->pCpioReader = &pThis->CpioReader;
            pBaseObjData->ObjInfo     = *pInfo;

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
        rc = RTStrDupEx(ppszName, pThis->CpioReader.szName);
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
 * CPIO filesystem stream operations.
 */
static const RTVFSFSSTREAMOPS rtZipCpioFssOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_FS_STREAM,
        "CpioFsStream",
        rtZipCpioFss_Close,
        rtZipCpioFss_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSFSSTREAMOPS_VERSION,
    0,
    rtZipCpioFss_Next,
    NULL,
    NULL,
    NULL,
    RTVFSFSSTREAMOPS_VERSION
};


/**
 * Internal function use both by RTZipCpioFsStreamFromIoStream() and by
 * RTZipCpioFsStreamForFile() in updating mode.
 */
DECLHIDDEN(void) rtZipCpioReaderInit(PRTZIPCPIOFSSTREAM pThis, RTVFSIOSTREAM hVfsIos, uint64_t offStart)
{
    pThis->hVfsIos              = hVfsIos;
    pThis->hVfsCurObj           = NIL_RTVFSOBJ;
    pThis->pCurIosData          = NULL;
    pThis->offStart             = offStart;
    pThis->offNextHdr           = offStart;
    pThis->fEndOfStream         = false;
    pThis->rcFatal              = VINF_SUCCESS;

    /* Don't check if it's a CPIO stream here, do that in the
       rtZipCpioFss_Next. */
}


RTDECL(int) RTZipCpioFsStreamFromIoStream(RTVFSIOSTREAM hVfsIosIn, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss)
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
    PRTZIPCPIOFSSTREAM pThis;
    RTVFSFSSTREAM      hVfsFss;
    int rc = RTVfsNewFsStream(&rtZipCpioFssOps, sizeof(*pThis), NIL_RTVFS, NIL_RTVFSLOCK, RTFILE_O_READ,
                              &hVfsFss, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        rtZipCpioReaderInit(pThis, hVfsIosIn, fFlags);
        *phVfsFss = hVfsFss;
        return VINF_SUCCESS;
    }

    RTVfsIoStrmRelease(hVfsIosIn);
    return rc;
}


/**
 * Used by RTZipCpioFsStreamTruncate to resolve @a hVfsObj.
 */
DECLHIDDEN(PRTZIPCPIOBASEOBJ) rtZipCpioFsStreamBaseObjToPrivate(PRTZIPCPIOFSSTREAM pThis, RTVFSOBJ hVfsObj)
{
    PRTZIPCPIOBASEOBJ pThisObj;
    RTVFSOBJTYPE enmType = RTVfsObjGetType(hVfsObj);
    switch (enmType)
    {
        case RTVFSOBJTYPE_IO_STREAM:
        {
            RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
            AssertReturn(hVfsIos != NIL_RTVFSIOSTREAM, NULL);
            PRTZIPCPIOIOSTREAM pThisStrm = (PRTZIPCPIOIOSTREAM)RTVfsIoStreamToPrivate(hVfsIos, &g_rtZipCpioFssIosOps);
            RTVfsIoStrmRelease(hVfsIos);
            pThisObj = &pThisStrm->BaseObj;
            break;
        }

        case RTVFSOBJTYPE_SYMLINK:
        {
            RTVFSSYMLINK hVfsSymlink = RTVfsObjToSymlink(hVfsObj);
            AssertReturn(hVfsSymlink != NIL_RTVFSSYMLINK, NULL);
            pThisObj = (PRTZIPCPIOBASEOBJ)RTVfsSymlinkToPrivate(hVfsSymlink, &g_rtZipCpioFssSymOps);
            RTVfsSymlinkRelease(hVfsSymlink);
            break;
        }

        case RTVFSOBJTYPE_BASE:
            pThisObj = (PRTZIPCPIOBASEOBJ)RTVfsObjToPrivate(hVfsObj, &g_rtZipCpioFssBaseObjOps);
            break;

        default:
            /** @todo implement.   */
            AssertFailedReturn(NULL);
    }

    AssertReturn(pThisObj->pCpioReader == &pThis->CpioReader, NULL);
    return pThisObj;
}

