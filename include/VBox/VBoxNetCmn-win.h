/* $Id: VBoxNetCmn-win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetCmn-win.h - NDIS6 Networking Driver Common Definitions, Windows-specific code.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_VBoxNetCmn_win_h
#define VBOX_INCLUDED_VBoxNetCmn_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/log.h> /* for LOG_ENABLED */


DECLHIDDEN(void) vboxNetCmnWinDumpOidRequest(const char *pcszFunction, PNDIS_OID_REQUEST pRequest)
{
# ifdef LOG_ENABLED
    const char *pszType;
    const char *pszOid  = "unknown";

    switch (pRequest->RequestType)
    {
        case NdisRequestSetInformation: pszType = "set"; break;
        case NdisRequestMethod: pszType = "method"; break;
        case NdisRequestQueryInformation: pszType = "query info"; break;
        case NdisRequestQueryStatistics: pszType = "query stats"; break;
        default: pszType = "unknown";
    }
    switch (pRequest->DATA.SET_INFORMATION.Oid)
    {
        case OID_GEN_MAX_LINK_SPEED: pszOid = "OID_GEN_MAX_LINK_SPEED"; break;
        case OID_GEN_LINK_STATE: pszOid = "OID_GEN_LINK_STATE"; break;
        case OID_GEN_LINK_PARAMETERS: pszOid = "OID_GEN_LINK_PARAMETERS"; break;
        case OID_GEN_MINIPORT_RESTART_ATTRIBUTES: pszOid = "OID_GEN_MINIPORT_RESTART_ATTRIBUTES"; break;
        case OID_GEN_ENUMERATE_PORTS: pszOid = "OID_GEN_ENUMERATE_PORTS"; break;
        case OID_GEN_PORT_STATE: pszOid = "OID_GEN_PORT_STATE"; break;
        case OID_GEN_PORT_AUTHENTICATION_PARAMETERS: pszOid = "OID_GEN_PORT_AUTHENTICATION_PARAMETERS"; break;
        case OID_GEN_INTERRUPT_MODERATION: pszOid = "OID_GEN_INTERRUPT_MODERATION"; break;
        case OID_GEN_PHYSICAL_MEDIUM_EX: pszOid = "OID_GEN_PHYSICAL_MEDIUM_EX"; break;
        case OID_GEN_SUPPORTED_LIST: pszOid = "OID_GEN_SUPPORTED_LIST"; break;
        case OID_GEN_HARDWARE_STATUS: pszOid = "OID_GEN_HARDWARE_STATUS"; break;
        case OID_GEN_MEDIA_SUPPORTED: pszOid = "OID_GEN_MEDIA_SUPPORTED"; break;
        case OID_GEN_MEDIA_IN_USE: pszOid = "OID_GEN_MEDIA_IN_USE"; break;
        case OID_GEN_MAXIMUM_LOOKAHEAD: pszOid = "OID_GEN_MAXIMUM_LOOKAHEAD"; break;
        case OID_GEN_MAXIMUM_FRAME_SIZE: pszOid = "OID_GEN_MAXIMUM_FRAME_SIZE"; break;
        case OID_GEN_LINK_SPEED: pszOid = "OID_GEN_LINK_SPEED"; break;
        case OID_GEN_TRANSMIT_BUFFER_SPACE: pszOid = "OID_GEN_TRANSMIT_BUFFER_SPACE"; break;
        case OID_GEN_RECEIVE_BUFFER_SPACE: pszOid = "OID_GEN_RECEIVE_BUFFER_SPACE"; break;
        case OID_GEN_TRANSMIT_BLOCK_SIZE: pszOid = "OID_GEN_TRANSMIT_BLOCK_SIZE"; break;
        case OID_GEN_RECEIVE_BLOCK_SIZE: pszOid = "OID_GEN_RECEIVE_BLOCK_SIZE"; break;
        case OID_GEN_VENDOR_ID: pszOid = "OID_GEN_VENDOR_ID"; break;
        case OID_GEN_VENDOR_DESCRIPTION: pszOid = "OID_GEN_VENDOR_DESCRIPTION"; break;
        case OID_GEN_VENDOR_DRIVER_VERSION: pszOid = "OID_GEN_VENDOR_DRIVER_VERSION"; break;
        case OID_GEN_CURRENT_PACKET_FILTER: pszOid = "OID_GEN_CURRENT_PACKET_FILTER"; break;
        case OID_GEN_CURRENT_LOOKAHEAD: pszOid = "OID_GEN_CURRENT_LOOKAHEAD"; break;
        case OID_GEN_DRIVER_VERSION: pszOid = "OID_GEN_DRIVER_VERSION"; break;
        case OID_GEN_MAXIMUM_TOTAL_SIZE: pszOid = "OID_GEN_MAXIMUM_TOTAL_SIZE"; break;
        case OID_GEN_PROTOCOL_OPTIONS: pszOid = "OID_GEN_PROTOCOL_OPTIONS"; break;
        case OID_GEN_MAC_OPTIONS: pszOid = "OID_GEN_MAC_OPTIONS"; break;
        case OID_GEN_MEDIA_CONNECT_STATUS: pszOid = "OID_GEN_MEDIA_CONNECT_STATUS"; break;
        case OID_GEN_MAXIMUM_SEND_PACKETS: pszOid = "OID_GEN_MAXIMUM_SEND_PACKETS"; break;
        case OID_GEN_SUPPORTED_GUIDS: pszOid = "OID_GEN_SUPPORTED_GUIDS"; break;
        case OID_GEN_NETWORK_LAYER_ADDRESSES: pszOid = "OID_GEN_NETWORK_LAYER_ADDRESSES"; break;
        case OID_GEN_TRANSPORT_HEADER_OFFSET: pszOid = "OID_GEN_TRANSPORT_HEADER_OFFSET"; break;
        case OID_GEN_PHYSICAL_MEDIUM: pszOid = "OID_GEN_PHYSICAL_MEDIUM"; break;
        case OID_GEN_MACHINE_NAME: pszOid = "OID_GEN_MACHINE_NAME"; break;
        case OID_GEN_VLAN_ID: pszOid = "OID_GEN_VLAN_ID"; break;
        case OID_GEN_RNDIS_CONFIG_PARAMETER: pszOid = "OID_GEN_RNDIS_CONFIG_PARAMETER"; break;
        case OID_GEN_NDIS_RESERVED_1: pszOid = "OID_GEN_NDIS_RESERVED_1"; break;
        case OID_GEN_NDIS_RESERVED_2: pszOid = "OID_GEN_NDIS_RESERVED_2"; break;
        case OID_GEN_NDIS_RESERVED_5: pszOid = "OID_GEN_NDIS_RESERVED_5"; break;
        case OID_GEN_MEDIA_CAPABILITIES: pszOid = "OID_GEN_MEDIA_CAPABILITIES"; break;
        case OID_GEN_DEVICE_PROFILE: pszOid = "OID_GEN_DEVICE_PROFILE"; break;
        case OID_GEN_FRIENDLY_NAME: pszOid = "OID_GEN_FRIENDLY_NAME"; break;
        case OID_802_3_ADD_MULTICAST_ADDRESS: pszOid = "OID_802_3_ADD_MULTICAST_ADDRESS"; break;
        case OID_802_3_DELETE_MULTICAST_ADDRESS: pszOid = "OID_802_3_DELETE_MULTICAST_ADDRESS"; break;
        case OID_802_3_PERMANENT_ADDRESS: pszOid = "OID_802_3_PERMANENT_ADDRESS"; break;
        case OID_802_3_CURRENT_ADDRESS: pszOid = "OID_802_3_CURRENT_ADDRESS"; break;
        case OID_802_3_MULTICAST_LIST: pszOid = "OID_802_3_MULTICAST_LIST"; break;
        case OID_802_3_MAXIMUM_LIST_SIZE: pszOid = "OID_802_3_MAXIMUM_LIST_SIZE"; break;
        case OID_802_3_MAC_OPTIONS: pszOid = "OID_802_3_MAC_OPTIONS"; break;
        case OID_TCP_TASK_OFFLOAD: pszOid = "OID_TCP_TASK_OFFLOAD"; break;
        case OID_TCP_TASK_IPSEC_ADD_SA: pszOid = "OID_TCP_TASK_IPSEC_ADD_SA"; break;
        case OID_TCP_TASK_IPSEC_ADD_UDPESP_SA: pszOid = "OID_TCP_TASK_IPSEC_ADD_UDPESP_SA"; break;
        case OID_TCP_TASK_IPSEC_DELETE_SA: pszOid = "OID_TCP_TASK_IPSEC_DELETE_SA"; break;
        case OID_TCP_TASK_IPSEC_DELETE_UDPESP_SA: pszOid = "OID_TCP_TASK_IPSEC_DELETE_UDPESP_SA"; break;

        case OID_GEN_STATISTICS: pszOid = "OID_GEN_STATISTICS"; break;
        case OID_GEN_BYTES_RCV: pszOid = "OID_GEN_BYTES_RCV"; break;
        case OID_GEN_BYTES_XMIT: pszOid = "OID_GEN_BYTES_XMIT"; break;
        case OID_GEN_RCV_DISCARDS: pszOid = "OID_GEN_RCV_DISCARDS"; break;
        case OID_GEN_XMIT_DISCARDS: pszOid = "OID_GEN_XMIT_DISCARDS"; break;
        case OID_GEN_XMIT_OK: pszOid = "OID_GEN_XMIT_OK"; break;
        case OID_GEN_RCV_OK: pszOid = "OID_GEN_RCV_OK"; break;
        case OID_GEN_XMIT_ERROR: pszOid = "OID_GEN_XMIT_ERROR"; break;
        case OID_GEN_RCV_ERROR: pszOid = "OID_GEN_RCV_ERROR"; break;
        case OID_GEN_RCV_NO_BUFFER: pszOid = "OID_GEN_RCV_NO_BUFFER"; break;
        case OID_GEN_DIRECTED_BYTES_XMIT: pszOid = "OID_GEN_DIRECTED_BYTES_XMIT"; break;
        case OID_GEN_DIRECTED_FRAMES_XMIT: pszOid = "OID_GEN_DIRECTED_FRAMES_XMIT"; break;
        case OID_GEN_MULTICAST_BYTES_XMIT: pszOid = "OID_GEN_MULTICAST_BYTES_XMIT"; break;
        case OID_GEN_MULTICAST_FRAMES_XMIT: pszOid = "OID_GEN_MULTICAST_FRAMES_XMIT"; break;
        case OID_GEN_BROADCAST_BYTES_XMIT: pszOid = "OID_GEN_BROADCAST_BYTES_XMIT"; break;
        case OID_GEN_BROADCAST_FRAMES_XMIT: pszOid = "OID_GEN_BROADCAST_FRAMES_XMIT"; break;
        case OID_GEN_DIRECTED_BYTES_RCV: pszOid = "OID_GEN_DIRECTED_BYTES_RCV"; break;
        case OID_GEN_DIRECTED_FRAMES_RCV: pszOid = "OID_GEN_DIRECTED_FRAMES_RCV"; break;
        case OID_GEN_MULTICAST_BYTES_RCV: pszOid = "OID_GEN_MULTICAST_BYTES_RCV"; break;
        case OID_GEN_MULTICAST_FRAMES_RCV: pszOid = "OID_GEN_MULTICAST_FRAMES_RCV"; break;
        case OID_GEN_BROADCAST_BYTES_RCV: pszOid = "OID_GEN_BROADCAST_BYTES_RCV"; break;
        case OID_GEN_BROADCAST_FRAMES_RCV: pszOid = "OID_GEN_BROADCAST_FRAMES_RCV"; break;
        case OID_GEN_RCV_CRC_ERROR: pszOid = "OID_GEN_RCV_CRC_ERROR"; break;
        case OID_GEN_TRANSMIT_QUEUE_LENGTH: pszOid = "OID_GEN_TRANSMIT_QUEUE_LENGTH"; break;
        case OID_GEN_INIT_TIME_MS: pszOid = "OID_GEN_INIT_TIME_MS"; break;
        case OID_GEN_RESET_COUNTS: pszOid = "OID_GEN_RESET_COUNTS"; break;
        case OID_GEN_MEDIA_SENSE_COUNTS: pszOid = "OID_GEN_MEDIA_SENSE_COUNTS"; break;

        case OID_PNP_CAPABILITIES: pszOid = "OID_PNP_CAPABILITIES"; break;
        case OID_PNP_SET_POWER: pszOid = "OID_PNP_SET_POWER"; break;
        case OID_PNP_QUERY_POWER: pszOid = "OID_PNP_QUERY_POWER"; break;
        case OID_PNP_ADD_WAKE_UP_PATTERN: pszOid = "OID_PNP_ADD_WAKE_UP_PATTERN"; break;
        case OID_PNP_REMOVE_WAKE_UP_PATTERN: pszOid = "OID_PNP_REMOVE_WAKE_UP_PATTERN"; break;
        case OID_PNP_WAKE_UP_PATTERN_LIST: pszOid = "OID_PNP_WAKE_UP_PATTERN_LIST"; break;
        case OID_PNP_ENABLE_WAKE_UP: pszOid = "OID_PNP_ENABLE_WAKE_UP"; break;
        case OID_PNP_WAKE_UP_OK: pszOid = "OID_PNP_WAKE_UP_OK"; break;
        case OID_PNP_WAKE_UP_ERROR: pszOid = "OID_PNP_WAKE_UP_ERROR"; break;
    }
    Log(("%s: %s(0x%x) %s(0x%x)\n", pcszFunction, pszType, pRequest->RequestType, pszOid, pRequest->DATA.SET_INFORMATION.Oid));
# else
    RT_NOREF2(pcszFunction, pRequest);
# endif
}

#endif /* !VBOX_INCLUDED_VBoxNetCmn_win_h */
