/* $Id: VBoxNetLib.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetUDP - IntNet Client Library.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_NetLib_VBoxNetLib_h
#define VBOX_INCLUDED_SRC_NetLib_VBoxNetLib_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/net.h>
#include <VBox/intnet.h>

RT_C_DECLS_BEGIN


/**
 * Header pointers optionally returned by VBoxNetUDPMatch.
 */
typedef struct VBOXNETUDPHDRS
{
    PCRTNETETHERHDR     pEth;           /**< Pointer to the ethernet header. */
    PCRTNETIPV4         pIpv4;          /**< Pointer to the IPV4 header if IPV4 packet. */
    PCRTNETUDP          pUdp;           /**< Pointer to the UDP header. */
} VBOXNETUDPHDRS;
/** Pointer to a VBOXNETUDPHDRS structure. */
typedef VBOXNETUDPHDRS *PVBOXNETUDPHDRS;


/** @name VBoxNetUDPMatch flags.
 * @{ */
#define VBOXNETUDP_MATCH_UNICAST            RT_BIT_32(0)
#define VBOXNETUDP_MATCH_BROADCAST          RT_BIT_32(1)
#define VBOXNETUDP_MATCH_CHECKSUM           RT_BIT_32(2)
#define VBOXNETUDP_MATCH_REQUIRE_CHECKSUM   RT_BIT_32(3)
#define VBOXNETUDP_MATCH_PRINT_STDERR       RT_BIT_32(31)
/** @}  */

void *  VBoxNetUDPMatch(PINTNETBUF pBuf, unsigned uDstPort, PCRTMAC pDstMac, uint32_t fFlags, PVBOXNETUDPHDRS pHdrs, size_t *pcb);
int     VBoxNetUDPUnicast(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf,
                          RTNETADDRIPV4 SrcIPv4Addr, PCRTMAC SrcMacAddr, unsigned uSrcPort,
                          RTNETADDRIPV4 DstIPv4Addr, PCRTMAC DstMacAddr, unsigned uDstPort,
                          void const *pvData, size_t cbData);
int     VBoxNetUDPBroadcast(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf,
                            RTNETADDRIPV4 SrcIPv4Addr, PCRTMAC SrcMacAddr, unsigned uSrcPort,
                            unsigned uDstPort,
                            void const *pvData, size_t cbData);

bool    VBoxNetArpHandleIt(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf, PCRTMAC pMacAddr, RTNETADDRIPV4 IPv4Addr);

int     VBoxNetIntIfFlush(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf);
int     VBoxNetIntIfRingWriteFrame(PINTNETBUF pBuf, PINTNETRINGBUF pRingBuf, size_t cSegs, PCINTNETSEG paSegs);
int     VBoxNetIntIfSend(PSUPDRVSESSION pSession, INTNETIFHANDLE hIf, PINTNETBUF pBuf, size_t cSegs, PCINTNETSEG paSegs, bool fFlush);


RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_NetLib_VBoxNetLib_h */

