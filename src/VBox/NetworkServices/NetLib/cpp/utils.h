/* $Id: utils.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NetLib/cpp/utils.h
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_NetLib_cpp_utils_h
#define VBOX_INCLUDED_SRC_NetLib_cpp_utils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/** less operator for IPv4 addresess */
DECLINLINE(bool) operator <(const RTNETADDRIPV4 &lhs, const RTNETADDRIPV4 &rhs)
{
    return RT_N2H_U32(lhs.u) < RT_N2H_U32(rhs.u);
}

/** greater operator for IPv4 addresess */
DECLINLINE(bool) operator >(const RTNETADDRIPV4 &lhs, const RTNETADDRIPV4 &rhs)
{
    return RT_N2H_U32(lhs.u) > RT_N2H_U32(rhs.u);
}

/**  Compares MAC addresses */
DECLINLINE(bool) operator== (const RTMAC &lhs, const RTMAC &rhs)
{
    return lhs.au16[0] == rhs.au16[0]
        && lhs.au16[1] == rhs.au16[1]
        && lhs.au16[2] == rhs.au16[2];
}

#endif /* !VBOX_INCLUDED_SRC_NetLib_cpp_utils_h */

