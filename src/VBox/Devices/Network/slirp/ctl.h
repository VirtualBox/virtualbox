/* $Id: ctl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - IP subnet constants.
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

#ifndef _SLIRP_CTL_H_
#define _SLIRP_CTL_H_

#define CTL_CMD         0
#define CTL_EXEC        1
#define CTL_ALIAS       2
#define CTL_DNS         3
#define CTL_TFTP        4
#define CTL_GUEST       15
#define CTL_BROADCAST   255


#define CTL_CHECK_NETWORK(x) (((x) & RT_H2N_U32(pData->netmask)) == pData->special_addr.s_addr)

#define CTL_CHECK(x, ctl) (   ((RT_N2H_U32((x)) & ~pData->netmask) == (ctl)) \
                           && CTL_CHECK_NETWORK(x))

#define CTL_CHECK_MINE(x) (   CTL_CHECK(x, CTL_ALIAS)      \
                           || CTL_CHECK(x, CTL_DNS)        \
                           || CTL_CHECK(x, CTL_TFTP))

#define CTL_CHECK_BROADCAST(x) CTL_CHECK((x), ~pData->netmask)


#endif /* _SLIRP_CTL_H_ */
