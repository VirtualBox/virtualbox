/* $Id: mz.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, MZ Executable Header.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_mz_h
#define IPRT_INCLUDED_formats_mz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>

typedef struct _IMAGE_DOS_HEADER
{
    uint16_t   e_magic;
    uint16_t   e_cblp;
    uint16_t   e_cp;
    uint16_t   e_crlc;
    uint16_t   e_cparhdr;
    uint16_t   e_minalloc;
    uint16_t   e_maxalloc;
    uint16_t   e_ss;
    uint16_t   e_sp;
    uint16_t   e_csum;
    uint16_t   e_ip;
    uint16_t   e_cs;
    uint16_t   e_lfarlc;
    uint16_t   e_ovno;
    uint16_t   e_res[4];
    uint16_t   e_oemid;
    uint16_t   e_oeminfo;
    uint16_t   e_res2[10];
    uint32_t   e_lfanew;
} IMAGE_DOS_HEADER;
AssertCompileSize(IMAGE_DOS_HEADER, 0x40);
typedef IMAGE_DOS_HEADER *PIMAGE_DOS_HEADER;

#ifndef IMAGE_DOS_SIGNATURE
# define IMAGE_DOS_SIGNATURE ('M' | ('Z' << 8)) /* fix endianness */
#endif


#endif /* !IPRT_INCLUDED_formats_mz_h */

