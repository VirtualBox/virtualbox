/* $Id: UsbTestServiceProtocol.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestService - Remote USB test configuration and execution server, Protocol helpers.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/asm.h>
#include <iprt/cdefs.h>

#include "UsbTestServiceProtocol.h"



/**
 * Converts a UTS packet header from host to network byte order.
 *
 * @param   pPktHdr          The packet header to convert.
 */
DECLINLINE(void) utsProtocolPktHdrH2N(PUTSPKTHDR pPktHdr)
{
    pPktHdr->cb     = RT_H2N_U32(pPktHdr->cb);
    pPktHdr->uCrc32 = RT_H2N_U32(pPktHdr->uCrc32);
}


/**
 * Converts a UTS packet header from network to host byte order.
 *
 * @param   pPktHdr          The packet header to convert.
 */
DECLINLINE(void) utsProtocolPktHdrN2H(PUTSPKTHDR pPktHdr)
{
    pPktHdr->cb     = RT_N2H_U32(pPktHdr->cb);
    pPktHdr->uCrc32 = RT_N2H_U32(pPktHdr->uCrc32);
}


/**
 * Converts a UTS status header from host to network byte order.
 *
 * @param   pPktHdr          The packet header to convert.
 */
DECLINLINE(void) utsProtocolStsHdrH2N(PUTSPKTSTS pPktHdr)
{
    utsProtocolPktHdrH2N(&pPktHdr->Hdr);
    pPktHdr->rcReq     = RT_H2N_U32(pPktHdr->rcReq);
    pPktHdr->cchStsMsg = RT_H2N_U32(pPktHdr->cchStsMsg);
}


/**
 * Converts a UTS status header from network to host byte order.
 *
 * @param   pPktHdr          The packet header to convert.
 */
DECLINLINE(void) utsProtocolStsHdrN2H(PUTSPKTSTS pPktHdr)
{
    utsProtocolPktHdrN2H(&pPktHdr->Hdr);
    pPktHdr->rcReq     = RT_N2H_U32(pPktHdr->rcReq);
    pPktHdr->cchStsMsg = RT_N2H_U32(pPktHdr->cchStsMsg);
}


DECLHIDDEN(void) utsProtocolReqH2N(PUTSPKTHDR pPktHdr)
{
    utsProtocolPktHdrH2N(pPktHdr);
}


DECLHIDDEN(void) utsProtocolReqN2H(PUTSPKTHDR pPktHdr)
{
    RT_NOREF1(pPktHdr);
}


DECLHIDDEN(void) utsProtocolRepH2N(PUTSPKTSTS pPktHdr)
{
    RT_NOREF1(pPktHdr);
}


DECLHIDDEN(void) utsProtocolRepN2H(PUTSPKTSTS pPktHdr)
{
    RT_NOREF1(pPktHdr);
}
