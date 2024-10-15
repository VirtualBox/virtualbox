/* $Id: tarvfsreader.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - TAR Virtual Filesystem.
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

#ifndef IPRT_INCLUDED_SRC_common_zip_tarvfsreader_h
#define IPRT_INCLUDED_SRC_common_zip_tarvfsreader_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "tar.h"


/**
 * TAR reader state machine states.
 */
typedef enum RTZIPTARREADERSTATE
{
    /** Invalid state. */
    RTZIPTARREADERSTATE_INVALID = 0,
    /** Expecting the next file/dir/whatever entry. */
    RTZIPTARREADERSTATE_FIRST,
    /** Expecting more zero headers or the end of the stream. */
    RTZIPTARREADERSTATE_ZERO,
    /** Expecting a GNU long name. */
    RTZIPTARREADERSTATE_GNU_LONGNAME,
    /** Expecting a GNU long link. */
    RTZIPTARREADERSTATE_GNU_LONGLINK,
    /** Expecting a normal header or another GNU specific one. */
    RTZIPTARREADERSTATE_GNU_NEXT,
    /** End of valid states (not included). */
    RTZIPTARREADERSTATE_END
} RTZIPTARREADERSTATE;

/**
 * Tar reader instance data.
 */
typedef struct RTZIPTARREADER
{
    /** Zero header counter. */
    uint32_t                cZeroHdrs;
    /** The state machine state. */
    RTZIPTARREADERSTATE     enmState;
    /** The type of the previous TAR header.
     * @remarks Same a enmType for the first header in the TAR stream. */
    RTZIPTARTYPE            enmPrevType;
    /** The type of the current TAR header. */
    RTZIPTARTYPE            enmType;
    /** The current header. */
    RTZIPTARHDR             Hdr;
    /** The expected long name/link length (GNU). */
    uint32_t                cbGnuLongExpect;
    /** The current long name/link length (GNU). */
    uint32_t                offGnuLongCur;
    /** The name of the current object.
     * This is for handling GNU and PAX long names. */
    char                    szName[RTPATH_MAX];
    /** The current link target if symlink or hardlink. */
    char                    szTarget[RTPATH_MAX];
} RTZIPTARREADER;
/** Pointer to the TAR reader instance data. */
typedef RTZIPTARREADER *PRTZIPTARREADER;

/**
 * Tar directory, character device, block device, fifo socket or symbolic link.
 */
typedef struct RTZIPTARBASEOBJ
{
    /** The stream offset of the (first) header in the input stream/file.  */
    RTFOFF                  offHdr;
    /** The stream offset of the first header of the next object (for truncating the
     * tar file after this object (updating)). */
    RTFOFF                  offNextHdr;
    /** Pointer to the reader instance data (resides in the filesystem
     * stream).
     * @todo Fix this so it won't go stale... Back ref from this obj to fss? */
    PRTZIPTARREADER         pTarReader;
    /** The object info with unix attributes. */
    RTFSOBJINFO             ObjInfo;
} RTZIPTARBASEOBJ;
/** Pointer to a TAR filesystem stream base object. */
typedef RTZIPTARBASEOBJ *PRTZIPTARBASEOBJ;


/**
 * Tar file represented as a VFS I/O stream.
 */
typedef struct RTZIPTARIOSTREAM
{
    /** The basic TAR object data. */
    RTZIPTARBASEOBJ         BaseObj;
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
} RTZIPTARIOSTREAM;
/** Pointer to a the private data of a TAR file I/O stream. */
typedef RTZIPTARIOSTREAM *PRTZIPTARIOSTREAM;


/**
 * Tar filesystem stream private data.
 */
typedef struct RTZIPTARFSSTREAM
{
    /** The input I/O stream. */
    RTVFSIOSTREAM           hVfsIos;

    /** The current object (referenced). */
    RTVFSOBJ                hVfsCurObj;
    /** Pointer to the private data if hVfsCurObj is representing a file. */
    PRTZIPTARIOSTREAM       pCurIosData;

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

    /** The TAR reader instance data. */
    RTZIPTARREADER          TarReader;
} RTZIPTARFSSTREAM;
/** Pointer to a the private data of a TAR filesystem stream. */
typedef RTZIPTARFSSTREAM *PRTZIPTARFSSTREAM;

DECLHIDDEN(void)                rtZipTarReaderInit(PRTZIPTARFSSTREAM pThis, RTVFSIOSTREAM hVfsIos, uint64_t offStart);
DECL_HIDDEN_CALLBACK(int)       rtZipTarFss_Next(void *pvThis, char **ppszName, RTVFSOBJTYPE *penmType, PRTVFSOBJ phVfsObj);
DECLHIDDEN(PRTZIPTARBASEOBJ)    rtZipTarFsStreamBaseObjToPrivate(PRTZIPTARFSSTREAM pThis, RTVFSOBJ hVfsObj);

#endif /* !IPRT_INCLUDED_SRC_common_zip_tarvfsreader_h */

