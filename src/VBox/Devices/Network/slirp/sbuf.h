/* $Id: sbuf.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - sbuf declarations/defines.
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

/*
 * This code is based on:
 *
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#ifndef _SBUF_H_
#define _SBUF_H_

# define sbflush(sb) sbdrop((sb),(sb)->sb_cc)
# define sbspace(sb) ((sb)->sb_datalen - (sb)->sb_cc)
# define SBUF_LEN(sb) ((sb)->sb_cc)
# define SBUF_SIZE(sb) ((sb)->sb_datalen)


struct sbuf
{
    u_int   sb_cc;          /* actual chars in buffer */
    u_int   sb_datalen;     /* Length of data  */
    char    *sb_wptr;       /* write pointer. points to where the next
                             * bytes should be written in the sbuf */
    char    *sb_rptr;       /* read pointer. points to where the next
                             * byte should be read from the sbuf */
    char    *sb_data;       /* Actual data */
};

void sbfree (struct sbuf *);
void sbdrop (struct sbuf *, int);
void sbreserve (PNATState, struct sbuf *, int);
void sbappend (PNATState, struct socket *, struct mbuf *);
void sbappendsb (PNATState, struct sbuf *, struct mbuf *);
void sbcopy (struct sbuf *, int, int, char *);
#endif
