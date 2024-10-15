/* $Id: swab.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VBox storage devices:
 * C++-safe replacements for some Linux byte order macros
 *
 * On Linux, DrvHostDVD.cpp includes <linux/cdrom.h>, which in turn
 * includes <linux/byteorder/swab.h>.  Unfortunately, that file is not very
 * C++ friendly, and our C++ compiler refuses to look at it.  The solution
 * is to define _LINUX_BYTEORDER_SWAB_H, which prevents that file's contents
 * from getting included at all, and to provide, in this file, our own
 * C++-proof versions of the macros which are needed by <linux/cdrom.h>
 * before we include that file.  We actually provide them as inline
 * functions, due to the way they get resolved in the original.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VBOX_INCLUDED_SRC_Storage_swab_h
#define VBOX_INCLUDED_SRC_Storage_swab_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define _LINUX_BYTEORDER_SWAB_H
#define _LINUX_BYTEORDER_SWABB_H

#include <asm/types.h>

/* Sorry for the unnecessary brackets here, but I really think
   readability requires them */
static __inline__ __u16 __swab16p(const __u16 *px)
{
    __u16 x = *px;
    return ((x & 0xff) << 8) | ((x >> 8) & 0xff);
}

static __inline__ __u32 __swab32p(const __u32 *px)
{
    __u32 x = *px;
    return ((x & 0xff) << 24) | ((x & 0xff00) << 8)
           | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff);
}

static __inline__ __u64 __swab64p(const __u64 *px)
{
    __u64 x = *px;
    return ((x & 0xff) << 56) | ((x & 0xff00) << 40)
           | ((x & 0xff0000) << 24) | ((x & 0xff000000) << 8)
           | ((x >> 8) & 0xff000000) | ((x >> 24) & 0xff0000)
           | ((x >> 40) & 0xff00) | ((x >> 56) & 0xff);
}

#endif /* !VBOX_INCLUDED_SRC_Storage_swab_h */
