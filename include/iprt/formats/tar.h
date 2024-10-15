/* $Id: tar.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef IPRT_INCLUDED_formats_tar_h
#define IPRT_INCLUDED_formats_tar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>

/** @name RTZIPTARHDRPOSIX::typeflag
 * @{  */
#define RTZIPTAR_TF_OLDNORMAL   '\0' /**< Normal disk file, Unix compatible */
#define RTZIPTAR_TF_NORMAL      '0'  /**< Normal disk file */
#define RTZIPTAR_TF_LINK        '1'  /**< Link to previously dumped file */
#define RTZIPTAR_TF_SYMLINK     '2'  /**< Symbolic link */
#define RTZIPTAR_TF_CHR         '3'  /**< Character special file */
#define RTZIPTAR_TF_BLK         '4'  /**< Block special file */
#define RTZIPTAR_TF_DIR         '5'  /**< Directory */
#define RTZIPTAR_TF_FIFO        '6'  /**< FIFO special file */
#define RTZIPTAR_TF_CONTIG      '7'  /**< Contiguous file */

#define RTZIPTAR_TF_X_HDR       'x'  /**< Extended header. */
#define RTZIPTAR_TF_X_GLOBAL    'g'  /**< Global extended header. */

#define RTZIPTAR_TF_SOLARIS_XHDR    'X'

#define RTZIPTAR_TF_GNU_DUMPDIR     'D'
#define RTZIPTAR_TF_GNU_LONGLINK    'K' /**< GNU long link header. */
#define RTZIPTAR_TF_GNU_LONGNAME    'L' /**< GNU long name header. */
#define RTZIPTAR_TF_GNU_MULTIVOL    'M'
#define RTZIPTAR_TF_GNU_SPARSE      'S'
#define RTZIPTAR_TF_GNU_VOLDHR      'V'
/** @} */

/** Maximum length of a tar filename, excluding the terminating '\0'. More
 * does not fit into a tar record. */
#define RTZIPTAR_NAME_MAX           99


/**
 * The ancient tar header.
 *
 * The posix and gnu headers are compatible with the members up to and including
 * link name, from there on they differ.
 */
typedef struct RTZIPTARHDRANCIENT
{
    char    name[100];
    char    mode[8];
    char    uid[8];
    char    gid[8];
    char    size[12];
    char    mtime[12];
    char    chksum[8];
    char    typeflag;
    char    linkname[100];              /**< Was called linkflag. */
    char    unused[8+64+16+155+12];
} RTZIPTARHDRANCIENT;
AssertCompileSize(RTZIPTARHDRANCIENT, 512);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, name,        0);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, mode,      100);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, uid,       108);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, gid,       116);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, size,      124);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, mtime,     136);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, chksum,    148);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, typeflag,  156);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, linkname,  157);
AssertCompileMemberOffset(RTZIPTARHDRANCIENT, unused,    257);


/** The uniform standard tape archive format magic value. */
#define RTZIPTAR_USTAR_MAGIC        "ustar"
/** The ustar version string.
 * @remarks The terminator character is not part of the field.  */
#define RTZIPTAR_USTAR_VERSION      "00"

/** The GNU magic + version value. */
#define RTZIPTAR_GNU_MAGIC          "ustar  "


/**
 * The posix header (according to SuS).
 */
typedef struct RTZIPTARHDRPOSIX
{
    char    name[100];
    char    mode[8];
    char    uid[8];
    char    gid[8];
    char    size[12];
    char    mtime[12];
    char    chksum[8];
    char    typeflag;
    char    linkname[100];
    char    magic[6];
    char    version[2];
    char    uname[32];
    char    gname[32];
    char    devmajor[8];
    char    devminor[8];
    char    prefix[155];
    char    unused[12];
} RTZIPTARHDRPOSIX;
AssertCompileSize(RTZIPTARHDRPOSIX, 512);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, name,        0);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, mode,      100);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, uid,       108);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, gid,       116);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, size,      124);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, mtime,     136);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, chksum,    148);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, typeflag,  156);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, linkname,  157);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, magic,     257);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, version,   263);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, uname,     265);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, gname,     297);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, devmajor,  329);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, devminor,  337);
AssertCompileMemberOffset(RTZIPTARHDRPOSIX, prefix,    345);

/**
 * GNU sparse data segment descriptor.
 */
typedef struct RTZIPTARGNUSPARSE
{
    char    offset[12];     /**< Absolute offset relative to the start of the file. */
    char    numbytes[12];
} RTZIPTARGNUSPARSE;
AssertCompileSize(RTZIPTARGNUSPARSE, 24);
AssertCompileMemberOffset(RTZIPTARGNUSPARSE, offset,    0);
AssertCompileMemberOffset(RTZIPTARGNUSPARSE, numbytes,  12);
/** Pointer to a GNU sparse data segment descriptor. */
typedef RTZIPTARGNUSPARSE *PRTZIPTARGNUSPARSE;
/** Pointer to a const GNU sparse data segment descriptor. */
typedef RTZIPTARGNUSPARSE *PCRTZIPTARGNUSPARSE;

/**
 * The GNU header.
 */
typedef struct RTZIPTARHDRGNU
{
    char    name[100];
    char    mode[8];
    char    uid[8];
    char    gid[8];
    char    size[12];
    char    mtime[12];
    char    chksum[8];
    char    typeflag;
    char    linkname[100];
    char    magic[8];
    char    uname[32];
    char    gname[32];
    char    devmajor[8];
    char    devminor[8];
    char    atime[12];
    char    ctime[12];
    char    offset[12];         /**< for multi-volume? */
    char    longnames[4];       /**< Seems to be unused. */
    char    unused[1];
    RTZIPTARGNUSPARSE sparse[4];
    char    isextended; /**< More headers about sparse stuff if binary value 1. */
    char    realsize[12];
    char    unused2[17];
} RTZIPTARHDRGNU;
AssertCompileSize(RTZIPTARHDRGNU, 512);
AssertCompileMemberOffset(RTZIPTARHDRGNU, name,        0);
AssertCompileMemberOffset(RTZIPTARHDRGNU, mode,      100);
AssertCompileMemberOffset(RTZIPTARHDRGNU, uid,       108);
AssertCompileMemberOffset(RTZIPTARHDRGNU, gid,       116);
AssertCompileMemberOffset(RTZIPTARHDRGNU, size,      124);
AssertCompileMemberOffset(RTZIPTARHDRGNU, mtime,     136);
AssertCompileMemberOffset(RTZIPTARHDRGNU, chksum,    148);
AssertCompileMemberOffset(RTZIPTARHDRGNU, typeflag,  156);
AssertCompileMemberOffset(RTZIPTARHDRGNU, linkname,  157);
AssertCompileMemberOffset(RTZIPTARHDRGNU, magic,     257);
AssertCompileMemberOffset(RTZIPTARHDRGNU, uname,     265);
AssertCompileMemberOffset(RTZIPTARHDRGNU, gname,     297);
AssertCompileMemberOffset(RTZIPTARHDRGNU, devmajor,  329);
AssertCompileMemberOffset(RTZIPTARHDRGNU, devminor,  337);
AssertCompileMemberOffset(RTZIPTARHDRGNU, atime,     345);
AssertCompileMemberOffset(RTZIPTARHDRGNU, ctime,     357);
AssertCompileMemberOffset(RTZIPTARHDRGNU, offset,    369);
AssertCompileMemberOffset(RTZIPTARHDRGNU, longnames, 381);
AssertCompileMemberOffset(RTZIPTARHDRGNU, unused,    385);
AssertCompileMemberOffset(RTZIPTARHDRGNU, sparse,    386);
AssertCompileMemberOffset(RTZIPTARHDRGNU, isextended,482);
AssertCompileMemberOffset(RTZIPTARHDRGNU, realsize,  483);
AssertCompileMemberOffset(RTZIPTARHDRGNU, unused2,   495);


/**
 * GNU sparse header.
 */
typedef struct RTZIPTARHDRGNUSPARSE
{
    RTZIPTARGNUSPARSE sp[21];
    char    isextended;
    char    unused[7];
} RTZIPTARHDRGNUSPARSE;
AssertCompileSize(RTZIPTARHDRGNUSPARSE, 512);
AssertCompileMemberOffset(RTZIPTARHDRGNUSPARSE, sp,         0);
AssertCompileMemberOffset(RTZIPTARHDRGNUSPARSE, isextended, 504);
AssertCompileMemberOffset(RTZIPTARHDRGNUSPARSE, unused,     505);


/**
 * The bits common to posix and GNU.
 */
typedef struct RTZIPTARHDRCOMMON
{
    char    name[100];
    char    mode[8];
    char    uid[8];
    char    gid[8];
    char    size[12];
    char    mtime[12];
    char    chksum[8];
    char    typeflag;
    char    linkname[100];
    char    magic[6];
    char    version[2];
    char    uname[32];
    char    gname[32];
    char    devmajor[8];
    char    devminor[8];
    char    not_common[155+12];
} RTZIPTARHDRCOMMON;
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, name,      RTZIPTARHDRPOSIX, name);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, mode,      RTZIPTARHDRPOSIX, mode);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, uid,       RTZIPTARHDRPOSIX, uid);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, gid,       RTZIPTARHDRPOSIX, gid);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, size,      RTZIPTARHDRPOSIX, size);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, mtime,     RTZIPTARHDRPOSIX, mtime);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, chksum,    RTZIPTARHDRPOSIX, chksum);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, typeflag,  RTZIPTARHDRPOSIX, typeflag);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, linkname,  RTZIPTARHDRPOSIX, linkname);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, magic,     RTZIPTARHDRPOSIX, magic);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, version,   RTZIPTARHDRPOSIX, version);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, uname,     RTZIPTARHDRPOSIX, uname);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, gname,     RTZIPTARHDRPOSIX, gname);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, devmajor,  RTZIPTARHDRPOSIX, devmajor);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, devminor,  RTZIPTARHDRPOSIX, devminor);

AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, name,      RTZIPTARHDRGNU, name);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, mode,      RTZIPTARHDRGNU, mode);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, uid,       RTZIPTARHDRGNU, uid);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, gid,       RTZIPTARHDRGNU, gid);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, size,      RTZIPTARHDRGNU, size);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, mtime,     RTZIPTARHDRGNU, mtime);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, chksum,    RTZIPTARHDRGNU, chksum);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, typeflag,  RTZIPTARHDRGNU, typeflag);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, linkname,  RTZIPTARHDRGNU, linkname);
AssertCompileMembersAtSameOffset(     RTZIPTARHDRCOMMON, magic,     RTZIPTARHDRGNU, magic);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, uname,     RTZIPTARHDRGNU, uname);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, gname,     RTZIPTARHDRGNU, gname);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, devmajor,  RTZIPTARHDRGNU, devmajor);
AssertCompileMembersSameSizeAndOffset(RTZIPTARHDRCOMMON, devminor,  RTZIPTARHDRGNU, devminor);

#endif /* !IPRT_INCLUDED_formats_tar_h */

