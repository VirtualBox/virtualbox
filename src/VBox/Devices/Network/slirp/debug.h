/* $Id: debug.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - debug helpers (declarations/defines).
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

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <VBox/log.h>
/* we've excluded stdio.h */
#define FILE void

int debug_init (PNATState);
void ipstats (PNATState);
void tcpstats (PNATState);
void udpstats (PNATState);
void icmpstats (PNATState);
void mbufstats (PNATState);
void sockstats (PNATState);

#ifdef LOG_ENABLED
# define TCP_STATE_SWITCH_TO(tp, new_tcp_state) \
  do { \
      Log2(("%R[tcpcb793] switch to %R[tcpstate] -> %R[tcpstate]\n", (tp), (tp->t_state) ,(new_tcp_state))); \
      if ((tp)->t_socket) \
          Log2(("%R[tcpcb793] %R[natsock]\n", (tp), (tp)->t_socket)); \
      (tp)->t_state = (new_tcp_state); \
  } while (0)
#else
# define TCP_STATE_SWITCH_TO(tp, new_tcp_state) (tp)->t_state = (new_tcp_state)
#endif

/* TCP CB state validity macro definitions
 * we need to be sure that TCP is in right state.
 * TCP_ACCEPTABLE_STATEX(tp, (X-states here))
 */
#ifdef DEBUG_vvl
# define TCP_ACCEPTABLE_STATE1(tp, tcp_state1) Assert((tp)->t_state == (tcp_state))
# define TCP_ACCEPTABLE_STATE2(tp, tcp_state1, tcp_state2) \
    Assert(   (tp)->t_state == (tcp_state1)  \
           || (tp)->t_state == (tcp_state2) ); \
#else
# define TCP_ACCEPTABLE_STATE1(tp, tcp_state1) do { } while(0)
# define TCP_ACCEPTABLE_STATE2(tp, tcp_state1, tcp_state2) do { } while(0)
#endif
#endif
