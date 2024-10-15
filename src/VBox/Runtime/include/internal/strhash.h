/* $Id: strhash.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal header containing inline string hashing functions.
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

#ifndef IPRT_INCLUDED_INTERNAL_strhash_h
#define IPRT_INCLUDED_INTERNAL_strhash_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>


/* sdbm:
   This algorithm was created for sdbm (a public-domain reimplementation of
   ndbm) database library. it was found to do well in scrambling bits,
   causing better distribution of the keys and fewer splits. it also happens
   to be a good general hashing function with good distribution. the actual
   function is hash(i) = hash(i - 1) * 65599 + str[i]; what is included below
   is the faster version used in gawk. [there is even a faster, duff-device
   version] the magic constant 65599 was picked out of thin air while
   experimenting with different constants, and turns out to be a prime.
   this is one of the algorithms used in berkeley db (see sleepycat) and
   elsewhere. */

/**
 * Hash string, return hash + length.
 */
DECLINLINE(uint32_t) sdbm(const char *str, size_t *pcch)
{
    uint8_t *pu8 = (uint8_t *)str;
    uint32_t hash = 0;
    int c;

    while ((c = *pu8++))
        hash = c + (hash << 6) + (hash << 16) - hash;

    *pcch = (uintptr_t)pu8 - (uintptr_t)str - 1;
    return hash;
}


/**
 * Hash up to N bytes, return hash + hashed length.
 */
DECLINLINE(uint32_t) sdbmN(const char *str, size_t cchMax, size_t *pcch)
{
    uint8_t *pu8 = (uint8_t *)str;
    uint32_t hash = 0;
    int c;

    while ((c = *pu8++) && cchMax-- > 0)
        hash = c + (hash << 6) + (hash << 16) - hash;

    *pcch = (uintptr_t)pu8 - (uintptr_t)str - 1;
    return hash;
}


/**
 * Incremental hashing.
 */
DECLINLINE(uint32_t) sdbmInc(const char *str, uint32_t hash)
{
    uint8_t *pu8 = (uint8_t *)str;
    int c;

    while ((c = *pu8++))
        hash = c + (hash << 6) + (hash << 16) - hash;

    return hash;
}

/**
 * Incremental hashing with length limitation.
 */
DECLINLINE(uint32_t) sdbmIncN(const char *psz, size_t cchMax, uint32_t uHash)
{
    uint8_t *pu8 = (uint8_t *)psz;
    int      c;

    while ((c = *pu8++) && cchMax-- > 0)
        uHash = c + (uHash << 6) + (uHash << 16) - uHash;

    return uHash;
}


#endif /* !IPRT_INCLUDED_INTERNAL_strhash_h */

