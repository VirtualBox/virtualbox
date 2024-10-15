/* $Id: tstRTR0CommonReq.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Common header defining the request packet.
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

#ifndef IPRT_INCLUDED_SRC_testcase_tstRTR0CommonReq_h
#define IPRT_INCLUDED_SRC_testcase_tstRTR0CommonReq_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/sup.h>

/**
 * Ring-0 test request packet.
 */
typedef struct RTTSTR0REQ
{
    SUPR0SERVICEREQHDR  Hdr;
    /** The message (output). */
    char                szMsg[2048];
} RTTSTR0REQ;
/** Pointer to a test request packet. */
typedef RTTSTR0REQ *PRTTSTR0REQ;


/** @name Standard requests.
 * @{ */
/** Positive sanity check. */
#define RTTSTR0REQ_SANITY_OK        1
/** Negative sanity check. */
#define RTTSTR0REQ_SANITY_FAILURE   2
/** The first user request.  */
#define RTTSTR0REQ_FIRST_USER       10
/** @}  */

#endif /* !IPRT_INCLUDED_SRC_testcase_tstRTR0CommonReq_h */

