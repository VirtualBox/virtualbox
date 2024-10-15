/** $Id: ext.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - some externals helpers
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef IN_BSD
# define zone_mbuf slirp_zone_mbuf(pData)
# define zone_clust slirp_zone_clust(pData)
# define zone_pack slirp_zone_pack(pData)
# define zone_jumbop slirp_zone_jumbop(pData)
# define zone_jumbo9 slirp_zone_jumbo9(pData)
# define zone_jumbo16 slirp_zone_jumbo16(pData)
# define zone_ext_refcnt slirp_zone_ext_refcnt(pData)
static inline uma_zone_t slirp_zone_mbuf(PNATState);
static inline uma_zone_t slirp_zone_clust(PNATState);
static inline uma_zone_t slirp_zone_pack(PNATState);
static inline uma_zone_t slirp_zone_jumbop(PNATState);
static inline uma_zone_t slirp_zone_jumbo9(PNATState);
static inline uma_zone_t slirp_zone_jumbo16(PNATState);
static inline uma_zone_t slirp_zone_ext_refcnt(PNATState);
#else
# undef zone_mbuf
# undef zone_clust
# undef zone_pack
# undef zone_jumbop
# undef zone_jumbo9
# undef zone_jumbo16
# undef zone_ext_refcnt

# define zone_mbuf pData->zone_mbuf
# define zone_clust pData->zone_clust
# define zone_pack pData->zone_pack
# define zone_jumbop pData->zone_jumbop
# define zone_jumbo9 pData->zone_jumbo9
# define zone_jumbo16 pData->zone_jumbo16
# define zone_ext_refcnt pData->zone_ext_refcnt
#endif

#ifndef _EXT_H_
#define _EXT_H_

# define fprintf vbox_slirp_fprintf
# define printf vbox_slirp_printf

# ifndef vbox_slirp_printfV
DECLINLINE(void) vbox_slirp_printV(char *format, va_list args)
{
    char buffer[1024];
    memset(buffer, 0, 1024);
    RTStrPrintfV(buffer, 1024, format, args);

    LogRel(("NAT:EXT: %s\n", buffer));
}
# endif

# ifndef vbox_slirp_printf
DECLINLINE(void) vbox_slirp_printf(char *format, ...)
{
    va_list args;
    va_start(args, format);
    vbox_slirp_printV(format, args);
    va_end(args);
}
# endif

# ifndef vbox_slirp_fprintf
DECLINLINE(void) vbox_slirp_fprintf(void *ignored, char *format, ...)
{
#  ifdef LOG_ENABLED
    va_list args;
    NOREF(ignored);
    va_start(args, format);
    vbox_slirp_printV(format, args);
    va_end(args);
#  else
    NOREF(format);
    NOREF(ignored);
#  endif
}
# endif

#endif

