/* $Id: tcpip.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - TCP/IP (declarations/defines).
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
 * Copyright (c) 1982, 1986, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)tcpip.h     8.1 (Berkeley) 6/10/93
 * tcpip.h,v 1.3 1994/08/21 05:27:40 paul Exp
 */

#ifndef _TCPIP_H_
#define _TCPIP_H_

/*
 * Tcp+ip header, after ip options removed.
 */
struct tcpiphdr
{
    struct      ipovly ti_i;            /* overlaid ip structure */
    struct      tcphdr ti_t;            /* tcp header */
};
AssertCompileSize(struct tcpiphdr, 40);
#define ti_next         ti_i.ih_next
#define ti_prev         ti_i.ih_prev
#define ti_x1           ti_i.ih_x1
#define ti_pr           ti_i.ih_pr
#define ti_len          ti_i.ih_len
#define ti_src          ti_i.ih_src
#define ti_dst          ti_i.ih_dst
#define ti_sport        ti_t.th_sport
#define ti_dport        ti_t.th_dport
#define ti_seq          ti_t.th_seq
#define ti_ack          ti_t.th_ack
#define ti_x2           ti_t.th_x2
#define ti_off          ti_t.th_off
#define ti_flags        ti_t.th_flags
#define ti_win          ti_t.th_win
#define ti_sum          ti_t.th_sum
#define ti_urp          ti_t.th_urp

/*
 * Just a clean way to get to the first byte
 * of the packet
 */
struct tcpiphdr_2
{
    struct tcpiphdr dummy;
    char first_char;
};

#endif
