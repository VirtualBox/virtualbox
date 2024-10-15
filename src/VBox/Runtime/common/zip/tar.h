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

#ifndef IPRT_INCLUDED_SRC_common_zip_tar_h
#define IPRT_INCLUDED_SRC_common_zip_tar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/formats/tar.h>

/**
 * Tar header union.
 */
typedef union RTZIPTARHDR
{
    /** Byte view. */
    char                ab[512];
    /** The standard header. */
    RTZIPTARHDRANCIENT      Ancient;
    /** The standard header. */
    RTZIPTARHDRPOSIX        Posix;
    /** The GNU header. */
    RTZIPTARHDRGNU          Gnu;
    /** The bits common to both GNU and the standard header. */
    RTZIPTARHDRCOMMON       Common;
    /** GNU sparse header. */
    RTZIPTARHDRGNUSPARSE    GnuSparse;
} RTZIPTARHDR;
AssertCompileSize(RTZIPTARHDR, 512);
/** Pointer to a tar file header. */
typedef RTZIPTARHDR *PRTZIPTARHDR;
/** Pointer to a const tar file header. */
typedef RTZIPTARHDR const *PCRTZIPTARHDR;


/**
 * Tar header type.
 */
typedef enum RTZIPTARTYPE
{
    /** Invalid type value. */
    RTZIPTARTYPE_INVALID = 0,
    /** Posix header.  */
    RTZIPTARTYPE_POSIX,
    /** The old GNU header, has layout conflicting with posix. */
    RTZIPTARTYPE_GNU,
    /** Ancient tar header which does not use anything beyond the magic. */
    RTZIPTARTYPE_ANCIENT,
    /** End of the valid type values (this is not valid).  */
    RTZIPTARTYPE_END,
    /** The usual type blow up.  */
    RTZIPTARTYPE_32BIT_HACK = 0x7fffffff
} RTZIPTARTYPE;
typedef RTZIPTARTYPE *PRTZIPTARTYPE;


/**
 * Calculates the TAR header checksums and detects if it's all zeros.
 *
 * @returns true if all zeros, false if not.
 * @param   pHdr            The header to checksum.
 * @param   pi32Unsigned    Where to store the checksum calculated using
 *                          unsigned chars.   This is the one POSIX specifies.
 * @param   pi32Signed      Where to store the checksum calculated using
 *                          signed chars.
 *
 * @remarks The reason why we calculate the checksum as both signed and unsigned
 *          has to do with various the char C type being signed on some hosts
 *          and unsigned on others.
 */
DECLINLINE(bool) rtZipTarCalcChkSum(PCRTZIPTARHDR pHdr, int32_t *pi32Unsigned, int32_t *pi32Signed)
{
    int32_t i32Unsigned = 0;
    int32_t i32Signed   = 0;

    /*
     * Sum up the entire header.
     */
    const char *pch    = (const char *)pHdr;
    const char *pchEnd = pch + sizeof(*pHdr);
    do
    {
        i32Unsigned += *(unsigned char *)pch;
        i32Signed   += *(signed   char *)pch;
    } while (++pch != pchEnd);

    /*
     * Check if it's all zeros and replace the chksum field with spaces.
     */
    bool const fZeroHdr = i32Unsigned == 0;

    pch    = pHdr->Common.chksum;
    pchEnd = pch + sizeof(pHdr->Common.chksum);
    do
    {
        i32Unsigned -= *(unsigned char *)pch;
        i32Signed   -= *(signed   char *)pch;
    } while (++pch != pchEnd);

    i32Unsigned += (unsigned char)' ' * sizeof(pHdr->Common.chksum);
    i32Signed   += (signed   char)' ' * sizeof(pHdr->Common.chksum);

    *pi32Unsigned = i32Unsigned;
    if (pi32Signed)
        *pi32Signed = i32Signed;
    return fZeroHdr;
}


#endif /* !IPRT_INCLUDED_SRC_common_zip_tar_h */

