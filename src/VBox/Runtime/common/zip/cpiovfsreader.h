/* $Id: cpiovfsreader.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - CPIO Virtual Filesystem.
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

#ifndef IPRT_INCLUDED_SRC_common_zip_cpiovfsreader_h
#define IPRT_INCLUDED_SRC_common_zip_cpiovfsreader_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/formats/cpio.h>


/**
 * CPIO archive type.
 */
typedef enum RTZIPCPIOTYPE
{
    /** Invalid type value. */
    RTZIPCPIOTYPE_INVALID = 0,
    /** Ancient binary archive.  */
    RTZIPCPIOTYPE_ANCIENT_BIN,
    /** Portable ASCII format as defined by SuSV2. */
    RTZIPCPIOTYPE_ASCII_SUSV2,
    /** "New" ASCII format. */
    RTZIPCPIOTYPE_ASCII_NEW,
    /** "New" ASCII format with checksumming. */
    RTZIPCPIOTYPE_ASCII_NEW_CHKSUM,
    /** End of the valid type values (this is not valid).  */
    RTZIPCPIOTYPE_END,
    /** The usual type blow up.  */
    RTZIPCPIOTYPE_32BIT_HACK = 0x7fffffff
} RTZIPCPIOTYPE;
typedef RTZIPCPIOTYPE *PRTZIPCPIOTYPE;


/**
 * CPIO reader instance data.
 */
typedef struct RTZIPCPIOREADER
{
    /** The object info with unix attributes. */
    RTFSOBJINFO             ObjInfo;
    /** The path length. */
    uint32_t                cbPath;
    /** The name of the current object. */
    char                    szName[RTPATH_MAX];
    /** The current link target if symlink. */
    char                    szTarget[RTPATH_MAX];
} RTZIPCPIOREADER;
/** Pointer to the CPIO reader instance data. */
typedef RTZIPCPIOREADER *PRTZIPCPIOREADER;

/**
 * CPIO directory, character device, block device, fifo socket or symbolic link.
 */
typedef struct RTZIPCPIOBASEOBJ
{
    /** The stream offset of the (first) header in the input stream/file.  */
    RTFOFF                  offHdr;
    /** The stream offset of the first header of the next object (for truncating the
     * tar file after this object (updating)). */
    RTFOFF                  offNextHdr;
    /** Pointer to the reader instance data (resides in the filesystem
     * stream).
     * @todo Fix this so it won't go stale... Back ref from this obj to fss? */
    PRTZIPCPIOREADER        pCpioReader;
    /** The object info with unix attributes. */
    RTFSOBJINFO             ObjInfo;
} RTZIPCPIOBASEOBJ;
/** Pointer to a CPIO filesystem stream base object. */
typedef RTZIPCPIOBASEOBJ *PRTZIPCPIOBASEOBJ;


/**
 * CPIO file represented as a VFS I/O stream.
 */
typedef struct RTZIPCPIOIOSTREAM
{
    /** The basic TAR object data. */
    RTZIPCPIOBASEOBJ        BaseObj;
    /** The number of bytes in the file. */
    RTFOFF                  cbFile;
    /** The current file position. */
    RTFOFF                  offFile;
    /** The start position in the hVfsIos (for seekable hVfsIos). */
    RTFOFF                  offStart;
    /** The number of padding bytes following the file. */
    uint32_t                cbPadding;
    /** Set if we've reached the end of this file. */
    bool                    fEndOfStream;
    /** The input I/O stream. */
    RTVFSIOSTREAM           hVfsIos;
} RTZIPCPIOIOSTREAM;
/** Pointer to a the private data of a CPIO file I/O stream. */
typedef RTZIPCPIOIOSTREAM *PRTZIPCPIOIOSTREAM;


/**
 * CPIO filesystem stream private data.
 */
typedef struct RTZIPCPIOFSSTREAM
{
    /** The input I/O stream. */
    RTVFSIOSTREAM           hVfsIos;

    /** The current object (referenced). */
    RTVFSOBJ                hVfsCurObj;
    /** Pointer to the private data if hVfsCurObj is representing a file. */
    PRTZIPCPIOIOSTREAM      pCurIosData;

    /** The start offset. */
    RTFOFF                  offStart;
    /** The offset of the next header. */
    RTFOFF                  offNextHdr;
    /** The offset of the first header for the current object.
     * When reaching the end, this will be the same as offNextHdr which will be
     * pointing to the first zero header */
    RTFOFF                  offCurHdr;

    /** Set if we've reached the end of the stream. */
    bool                    fEndOfStream;
    /** Set if we've encountered a fatal error. */
    int                     rcFatal;

    /** The CPIO reader instance data. */
    RTZIPCPIOREADER         CpioReader;
} RTZIPCPIOFSSTREAM;
/** Pointer to a the private data of a CPIO filesystem stream. */
typedef RTZIPCPIOFSSTREAM *PRTZIPCPIOFSSTREAM;

DECLHIDDEN(void)                rtZipCpioReaderInit(PRTZIPCPIOFSSTREAM pThis, RTVFSIOSTREAM hVfsIos, uint64_t offStart);
DECL_HIDDEN_CALLBACK(int)       rtZipCpioFss_Next(void *pvThis, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj);
DECLHIDDEN(PRTZIPCPIOBASEOBJ)   rtZipCpioFsStreamBaseObjToPrivate(PRTZIPCPIOFSSTREAM pThis, RTVFSOBJ hVfsObj);

#endif /* !IPRT_INCLUDED_SRC_common_zip_cpiovfsreader_h */

